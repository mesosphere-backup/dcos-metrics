package com.mesosphere.metrics.consumer.common;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import org.apache.avro.file.DataFileStream;
import org.apache.avro.io.DatumReader;
import org.apache.avro.specific.SpecificDatumReader;
import org.apache.kafka.clients.consumer.ConsumerRecord;
import org.apache.kafka.clients.consumer.ConsumerRecords;
import org.apache.kafka.clients.consumer.KafkaConsumer;
import org.apache.kafka.common.KafkaException;
import org.apache.kafka.common.serialization.ByteArrayDeserializer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import dcos.metrics.MetricList;

public class ConsumerRunner {

  public interface MetricOutputFactory {
    public MetricOutput getOutput() throws Exception;
  }

  private static final Logger LOGGER = LoggerFactory.getLogger(ConsumerRunner.class);

  private static class ConsumerRunnable implements Runnable {
    private final MetricOutput output;
    private final KafkaConsumer<byte[], byte[]> kafkaConsumer;
    private final ClientConfigs.ConsumerConfig consumerConfig;
    private final Stats.Values values;

    private ConsumerRunnable(
        MetricOutputFactory outputFactory,
        Map<String, Object> kafkaConfig,
        ClientConfigs.ConsumerConfig consumerConfig,
        Stats.Values values) throws Exception {
      ByteArrayDeserializer deserializer = new ByteArrayDeserializer();
      this.output = outputFactory.getOutput();
      this.kafkaConsumer = new KafkaConsumer<>(kafkaConfig, deserializer, deserializer);
      this.consumerConfig = consumerConfig;
      this.values = values;
    }

    @Override
    public void run() {
      if (consumerConfig.topicExact != null) {
        // subscribe to specific topic up-front
        List<String> topics = new ArrayList<>();
        topics.add(consumerConfig.topicExact);
        kafkaConsumer.subscribe(topics);
      }
      DatumReader<MetricList> datumReader = new SpecificDatumReader<MetricList>(MetricList.class);
      MetricList metricList = null;
      long lastTopicPollMs = 0;
      while (!values.isShutdown()) {
        long messages = 0;
        long bytes = 0;

        if (consumerConfig.topicPattern != null) {
          // Every so often, update the list of topics and re-subscribe if needed
          long currentTimeMs = System.currentTimeMillis();
          if (currentTimeMs - lastTopicPollMs > consumerConfig.topicPollPeriodMs) {
            lastTopicPollMs = currentTimeMs;
            try {
              updateSubscriptions();
            } catch (Throwable e) {
              values.setFatalError(e);
              break;
            }
          }
        }

        try {
          LOGGER.info("Waiting {}ms for messages", consumerConfig.pollTimeoutMs);
          ConsumerRecords<byte[], byte[]> kafkaRecords = kafkaConsumer.poll(consumerConfig.pollTimeoutMs);
          messages = kafkaRecords.count();
          for (ConsumerRecord<byte[], byte[]> kafkaRecord : kafkaRecords) {
            if (kafkaRecord.key() != null) {
              bytes += kafkaRecord.key().length; // not used, but count as received
            }
            bytes += kafkaRecord.value().length;

            LOGGER.info("Processing {} byte Kafka message", kafkaRecord.value().length);

            // Recreate the DataFileStream every time: Each Kafka record has a datafile header.
            DebuggingByteArrayInputStream inputStream = new DebuggingByteArrayInputStream(kafkaRecord.value());
            DataFileStream<MetricList> dataFileStream =
                new DataFileStream<MetricList>(inputStream, datumReader);
            long metricLists = 0;
            long tags = 0;
            long datapoints = 0;
            while (dataFileStream.hasNext()) {
              // reuse the same MetricList object. docs imply this is more efficient
              try {
                metricList = dataFileStream.next(metricList);
              } catch (IOException e) {
                inputStream.dumpState();
                dataFileStream.close();
                throw e;
              }

              output.append(metricList);

              metricLists++;
              tags += metricList.getTags().size();
              datapoints += metricList.getDatapoints().size();
            }
            dataFileStream.close();

            LOGGER.info("Kafka message ({} bytes) contained {} Datapoints and {} Tags across {} MetricLists",
                kafkaRecord.value().length, datapoints, tags, metricLists, kafkaRecord.value().length);
          }
          LOGGER.info("Got {} messages ({} bytes)", messages, bytes);
        } catch (KafkaException e) {
          values.setFatalError(e);
          break;
        } catch (Throwable e) {
          values.registerError(e);
        }
        output.flush(); // always flush, even if consumption fails (in case it failed after several append()s)
        values.incMessages(messages);
        values.incBytes(bytes);
      }
      kafkaConsumer.close();
    }

    void updateSubscriptions() {
      // note: timeout is configured by kafka's 'request.timeout.ms'
      // try to get list. exit if fails AND no preceding list
      // if matching list is empty (after successful fetch), fail in all cases
      // update subscription if needed. exit if
      Set<String> allTopics;
      try {
        LOGGER.info(String.format("Fetching topics and searching for matches of pattern '%s'", consumerConfig.topicPattern));
        allTopics = kafkaConsumer.listTopics().keySet();
      } catch (Throwable e) {
        values.registerError(e);
        if (!kafkaConsumer.subscription().isEmpty()) {
          // consumer is currently subscribed to something. don't let this failed lookup kill the consumer
          return;
        }
        // consumer has nothing subscribed. exit.
        throw new IllegalStateException(
            "Unable to proceed: failed to get initial list of topics, and consumer isn't subscribed to any", e);
      }

      List<String> matchingTopics = new ArrayList<>();
      for (String topic : allTopics) {
        if (consumerConfig.topicPattern.matcher(topic).matches()) {
          matchingTopics.add(topic);
        }
      }
      if (matchingTopics.isEmpty()) {
        throw new IllegalStateException(
            String.format("Unable to proceed: no matching topics exist (pattern: '%s', all topics: %s)",
                consumerConfig.topicPattern.toString(), Arrays.toString(allTopics.toArray())));
      }
      if (kafkaConsumer.subscription().equals(new HashSet<>(matchingTopics))) {
        LOGGER.info(String.format("Current topic subscription is up to date: %s", Arrays.toString(matchingTopics.toArray())));
      } else {
        LOGGER.info(String.format("Updating topic subscription to: %s", Arrays.toString(matchingTopics.toArray())));
        kafkaConsumer.subscribe(matchingTopics);
      }
    }
  }

  private static boolean runConsumers(MetricOutputFactory outputFactory, ConfigParser.Config config, Stats.PrintRunner printer) {
    ClientConfigs.ConsumerConfig consumerConfig = config.getConsumerConfig();
    if (consumerConfig == null) {
      LOGGER.error("Unable to load consumer config, exiting");
      return false;
    }

    ThreadRunner runner = new ThreadRunner(printer.getValues());
    runner.add("printStatsThread", printer);
    for (int i = 0; i < consumerConfig.threads; ++i) {
      ConsumerRunnable consumer;
      try {
        consumer = new ConsumerRunnable(outputFactory, config.getKafkaConfig(), consumerConfig, printer.getValues());
      } catch (Throwable e) {
        printer.getValues().registerError(e);
        return false;
      }
      runner.add("consumerThread-" + String.valueOf(i), consumer);
    }

    runner.runThreads();
    return !runner.isFatalError();
  }

  public static void run(MetricOutputFactory outputFactory) {
    ConfigParser.Config config = ConfigParser.getConfig();
    if (config == null) {
      LOGGER.error("Unable to load base config, exiting");
      System.exit(1);
    }

    ClientConfigs.StatsConfig statsConfig = config.getStatsConfig();
    if (statsConfig == null) {
      LOGGER.error("Unable to load stats config, exiting");
      System.exit(1);
    }
    Stats.PrintRunner printer = new Stats.PrintRunner(statsConfig);
    System.exit(runConsumers(outputFactory, config, printer) ? 0 : -1);
  }
}
