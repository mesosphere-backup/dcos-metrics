package com.mesosphere.metrics.consumer.common;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import org.apache.avro.AvroRuntimeException;
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
import dcos.metrics.Tag;

public class ConsumerRunner {

  public interface MetricOutputFactory {
    public MetricOutput getOutput() throws Exception;
  }

  private static final Logger LOGGER = LoggerFactory.getLogger(ConsumerRunner.class);

  private static class ConsumerRunnable implements Runnable {
    private static final String FRAMEWORK_NAME_TAG = "framework_name";

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
      if (!consumerConfig.exactTopics.isEmpty()) {
        // subscribe to specific topics up-front
        List<String> topics = new ArrayList<>();
        topics.addAll(consumerConfig.exactTopics);
        kafkaConsumer.subscribe(topics);
      }
      DatumReader<MetricList> datumReader = new SpecificDatumReader<MetricList>(MetricList.class);
      MetricList metricList = null;
      long lastTopicPollMs = 0;
      while (!values.isShutdown()) {
        long recordCount = 0;
        long byteCount = 0;
        long datapointCount = 0;
        long tagCount = 0;
        long metricListCount = 0;

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
          LOGGER.info("Waiting {}ms for records", consumerConfig.pollTimeoutMs);
          ConsumerRecords<byte[], byte[]> kafkaRecords =
              kafkaConsumer.poll(consumerConfig.pollTimeoutMs);
          recordCount = kafkaRecords.count();
          for (ConsumerRecord<byte[], byte[]> kafkaRecord : kafkaRecords) {
            if (kafkaRecord.key() != null) {
              byteCount += kafkaRecord.key().length; // not used, but count as received
            }
            byteCount += kafkaRecord.value().length;

            LOGGER.info("Processing {} byte Kafka record", kafkaRecord.value().length);

            // Recreate the DataFileStream every time: Each Kafka record has a datafile header.
            DebuggingByteArrayInputStream inputStream =
                new DebuggingByteArrayInputStream(kafkaRecord.value());
            DataFileStream<MetricList> dataFileStream =
                new DataFileStream<MetricList>(inputStream, datumReader);
            long recordMetricListCount = 0;
            long filteredMetricListCount = 0;
            long recordTagCount = 0;
            long recordDatapointCount = 0;
            try {
              while (dataFileStream.hasNext()) {
                // reuse the same MetricList object. docs imply this is more efficient
                metricList = dataFileStream.next(metricList);

                if (isFrameworkMatch(consumerConfig.frameworkWhitelist, metricList)) {
                  output.append(metricList);
                } else {
                  filteredMetricListCount++;
                }

                recordMetricListCount++;
                recordTagCount += metricList.getTags().size();
                recordDatapointCount += metricList.getDatapoints().size();
              }
            } catch (IOException | AvroRuntimeException e) {
              LOGGER.warn("Hit corrupt data in Kafka record ({} bytes) " +
                  "after extracting {} Datapoints and {} Tags across {} MetricLists",
                  kafkaRecord.value().length,
                  recordDatapointCount, recordTagCount, recordMetricListCount);
              inputStream.dumpState();
              dataFileStream.close();
              values.registerError(e);
              continue;
            }
            dataFileStream.close();

            LOGGER.info("Kafka record ({} bytes) " +
                "contained {} Datapoints and {} Tags across {} MetricLists ({} filtered)",
                kafkaRecord.value().length,
                recordDatapointCount, recordTagCount, recordMetricListCount, filteredMetricListCount);
            metricListCount += recordMetricListCount;
            tagCount += recordTagCount;
            datapointCount += recordDatapointCount;
          }
          LOGGER.info("Got {} Kafka records ({} bytes) " +
              "containing {} Datapoints and {} Tags across {} MetricLists",
              recordCount, byteCount,
              datapointCount, tagCount, metricListCount);
        } catch (KafkaException e) {
          values.setFatalError(e);
          break;
        } catch (Throwable e) {
          values.registerError(e);
        }
        output.flush(); // always flush, even if consumption fails (in case it failed after several append()s)
        values.incRecords(recordCount);
        values.incBytes(byteCount);
        values.incMetricLists(metricListCount);
        values.incTags(tagCount);
        values.incDatapoints(datapointCount);
      }
      kafkaConsumer.close();
    }

    private void updateSubscriptions() {
      // note: timeout is configured by kafka's 'request.timeout.ms'
      // try to get list. exit if fails AND no preceding list
      // if matching list is empty (after successful fetch), fail in all cases
      // update subscription if needed. exit if
      Set<String> allTopics;
      try {
        LOGGER.info("Fetching topics and searching for matches of pattern '{}'",
            consumerConfig.topicPattern);
        allTopics = kafkaConsumer.listTopics().keySet();
      } catch (Throwable e) {
        values.registerError(e);
        if (!kafkaConsumer.subscription().isEmpty()) {
          // consumer is currently subscribed to something. don't let this failed lookup kill the consumer
          return;
        }
        // consumer has nothing subscribed. exit.
        throw new IllegalStateException("Unable to proceed: " +
            "failed to get initial list of topics, and consumer isn't subscribed to any", e);
      }

      List<String> matchingTopics = new ArrayList<>();
      for (String topic : allTopics) {
        if (consumerConfig.topicPattern.matcher(topic).matches()) {
          matchingTopics.add(topic);
        }
      }
      if (matchingTopics.isEmpty()) {
        throw new IllegalStateException(String.format(
            "Unable to proceed: no matching topics exist (pattern: '%s', all topics: %s)",
            consumerConfig.topicPattern.toString(), Arrays.toString(allTopics.toArray())));
      }
      if (kafkaConsumer.subscription().equals(new HashSet<>(matchingTopics))) {
        LOGGER.info("Current topic subscription is up to date: {}",
            Arrays.toString(matchingTopics.toArray()));
      } else {
        LOGGER.info("Updating topic subscription to: {}",
            Arrays.toString(matchingTopics.toArray()));
        kafkaConsumer.subscribe(matchingTopics);
      }
    }

    private static boolean isFrameworkMatch(Set<String> frameworkWhitelist, MetricList metricList) {
      if (frameworkWhitelist.isEmpty()) {
        return true;
      }
      // support any null entry in whitelist (interpreted as 'no framework name'):
      return frameworkWhitelist.contains(getFrameworkName(metricList));
    }

    private static String getFrameworkName(MetricList metricList) {
      for (Tag tag : metricList.getTags()) {
        if (tag.getKey().equals(FRAMEWORK_NAME_TAG)) {
          return tag.getValue();
        }
      }
      return null;
    }
  }

  private static boolean runConsumers(
      MetricOutputFactory outputFactory, Stats.PrintRunner printer) {
    ClientConfigs.ConsumerConfig consumerConfig = ClientConfigs.ConsumerConfig.parseFromEnv();
    if (consumerConfig == null) {
      LOGGER.error("Unable to load consumer config, exiting");
      return false;
    }

    ThreadRunner runner = new ThreadRunner(printer.getValues());
    runner.add("printStatsThread", printer);
    for (int i = 0; i < consumerConfig.threads; ++i) {
      ConsumerRunnable consumer;
      try {
        consumer = new ConsumerRunnable(
            outputFactory,
            ClientConfigs.KafkaConfig.parseFromEnv().kafkaConfig,
            consumerConfig,
            printer.getValues());
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
    ClientConfigs.StatsConfig statsConfig = ClientConfigs.StatsConfig.parseFromEnv();
    if (statsConfig == null) {
      LOGGER.error("Unable to load stats config, exiting");
      System.exit(1);
    }
    Stats.PrintRunner printer = new Stats.PrintRunner(statsConfig);
    System.exit(runConsumers(outputFactory, printer) ? 0 : -1);
  }
}
