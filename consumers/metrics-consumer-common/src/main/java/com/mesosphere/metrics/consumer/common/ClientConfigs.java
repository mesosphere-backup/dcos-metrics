package com.mesosphere.metrics.consumer.common;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.Map.Entry;
import java.util.Set;
import java.util.regex.Pattern;


/**
 * Namespace for POJO classes containing configuration for various parts of our clients.
 */
public class ClientConfigs {

  private static final Logger LOGGER = LoggerFactory.getLogger(ClientConfigs.class);

  /**
   * POJO containing client bootstrap options.
   */
  public static class StartupConfig {
    public final String kafkaFrameworkName;

    /**
     * Returns {@code null} if parsing fails.
     */
    public static StartupConfig parseFromEnv() {
      try {
        return new StartupConfig(ArgUtils.parseStr("KAFKA_FRAMEWORK_NAME", "kafka"));
      } catch (Throwable e) {
        printFlagParseFailure(e);
        return null;
      }
    }

    private StartupConfig(String kafkaFrameworkName) {
      this.kafkaFrameworkName = kafkaFrameworkName;
    }
  }

  /**
   * POJO containing stats emitter options.
   */
  public static class StatsConfig {
    public final long printPeriodMs;

    /**
     * Returns {@code null} if parsing fails.
     */
    public static StatsConfig parseFromEnv() {
      try {
        return new StatsConfig(ArgUtils.parseLong("STATS_PRINT_PERIOD_MS", 5000));
      } catch (Throwable e) {
        printFlagParseFailure(e);
        return null;
      }
    }

    private StatsConfig(long printPeriodMs) {
      this.printPeriodMs = printPeriodMs;
    }
  }

  /**
   * POJO containing test consumer options.
   */
  public static class ConsumerConfig {
    public final long pollTimeoutMs;
    public final boolean printRecords;
    public final int threads;
    public final Set<String> frameworkWhitelist;
    public final List<String> exactTopics;
    /** Nullable */
    public final Pattern topicPattern;
    public final long topicPollPeriodMs;
    public final Map<String, Object> injectedTags;
    /**
     * Starts with "INJECT_TAG_..." => tags injected by the user.
     * The key is translated to lowercase, with underscores converted to periods.
     */
    private static final String INJECT_TAG_STARTS_WITH = "INJECT_TAG_";

    /**
     * Returns {@code null} if parsing fails.
     */
    public static ConsumerConfig parseFromEnv() {
      try {
        long pollTimeoutMs = ArgUtils.parseLong("POLL_TIMEOUT_MS", 1000);
        boolean printRecords = ArgUtils.parseBool("PRINT_RECORDS", false);
        int threads = ArgUtils.parseInt("CONSUMER_THREADS", 1);
        Set<String> frameworkWhitelist = new HashSet<>();
        //TODO(nick): How about periodically mapping framework name(s) to topic(s), and then
        //            automatically updating subscriptions to match those topic(s)?
        //            (this would be refreshed in the same place as KAFKA_TOPIC_PATTERN)
        for (String frameworkName : ArgUtils.parseStrList("FRAMEWORK_NAMES")) {
          if (frameworkName.equals("null")) { // special value for 'metrics without a framework name'
            frameworkWhitelist.add(null);
          } else {
            frameworkWhitelist.add(frameworkName);
          }
        }
        // Select all envvars which start with "INJECT_TAG_"
        Map<String, Object> injectedTags = new TreeMap<>();
        for (Entry<String, String> entry : System.getenv().entrySet()) {
          if (entry.getKey().startsWith(INJECT_TAG_STARTS_WITH)) {
            String injectedKey = entry.getKey().substring(
                    INJECT_TAG_STARTS_WITH.length(), entry.getKey().length());
            injectedTags.put(injectedKey.replace('_', '.').toLowerCase(), entry.getValue());
          }
        }
        List<String> exactTopics = ArgUtils.parseStrList("KAFKA_TOPICS");
        if (!exactTopics.isEmpty()) {
          // exact mode
          return new ConsumerConfig(
              pollTimeoutMs, printRecords, threads, frameworkWhitelist,
              exactTopics, injectedTags);
        }
        // regex mode
        return new ConsumerConfig(
            pollTimeoutMs, printRecords, threads, frameworkWhitelist,
            Pattern.compile(ArgUtils.parseStr("KAFKA_TOPIC_PATTERN", "metrics-.*")),
            ArgUtils.parseLong("KAFKA_TOPIC_POLL_PERIOD_MS", 60000), injectedTags);
      } catch (Throwable e) {
        printFlagParseFailure(e);
        return null;
      }
    }

    private ConsumerConfig(
        long pollTimeoutMs,
        boolean printRecords,
        int threads,
        Set<String> frameworkWhitelist,
        List<String> exactTopics,
        Map<String, Object> injectedTags) {
      this.pollTimeoutMs = pollTimeoutMs;
      this.printRecords = printRecords;
      this.threads = threads;
      this.frameworkWhitelist = frameworkWhitelist;
      this.exactTopics = exactTopics;
      this.topicPattern = null;
      this.topicPollPeriodMs = 0;
      this.injectedTags = injectedTags;
    }

    private ConsumerConfig(
        long pollTimeoutMs,
        boolean printRecords,
        int threads,
        Set<String> frameworkWhitelist,
        Pattern topicPattern, long topicPollPeriodMs,
        Map<String, Object> injectedTags) {
      this.pollTimeoutMs = pollTimeoutMs;
      this.printRecords = printRecords;
      this.threads = threads;
      this.frameworkWhitelist = frameworkWhitelist;
      this.exactTopics = new ArrayList<>();
      this.topicPattern = topicPattern;
      this.topicPollPeriodMs = topicPollPeriodMs;
      if (topicPattern == null) {
        LOGGER.error("Null topic pattern");
      }
      this.injectedTags = injectedTags;
    }
  }

  public static class KafkaConfig {
    public final Map<String, Object> kafkaConfig;

    /**
     * Starts with "KAFKA_OVERRIDE_..." => must be for Kafka
     * The key is translated to lowercase, with underscores converted to periods.
     */
    private static final String KAFKA_OVERRIDE_STARTS_WITH = "KAFKA_OVERRIDE_";

    private static final String KAFKA_BOOTSTRAP_SERVERS_KEY = "bootstrap.servers";
    private static final String ENV_KAFKA_BOOTSTRAP_SERVERS_KEY =
        KAFKA_OVERRIDE_STARTS_WITH + KAFKA_BOOTSTRAP_SERVERS_KEY.toUpperCase().replace('.', '_');

    /**
     * Returns config variables for passing to the Kafka Client.
     * Returns {@code null} if parsing fails, eg if a required setting is missing.
     */
    public static KafkaConfig parseFromEnv() {
      // Select all envvars which start with "KAFKA_OVERRIDE_"
      Map<String, Object> kafkaConfig = new TreeMap<>();
      for (Entry<String, String> entry : System.getenv().entrySet()) {
        if (entry.getKey().startsWith(KAFKA_OVERRIDE_STARTS_WITH)) {
          String kafkaKey = entry.getKey().substring(
              KAFKA_OVERRIDE_STARTS_WITH.length(), entry.getKey().length());
          kafkaConfig.put(kafkaKey.replace('_', '.').toLowerCase(), entry.getValue());
        }
      }

      // special case: get the bootstrap endpoints from the Kafka framework.
      // this can be overridden by providing "KAFKA_OVERRIDE_BOOTSTRAP_SERVERS=..." in env.
      ClientConfigs.StartupConfig startupConfig = ClientConfigs.StartupConfig.parseFromEnv();
      if (startupConfig == null) {
        LOGGER.error("Failed to parse startup config, exiting");
        return null;
      }
      if (!kafkaConfig.containsKey(KAFKA_BOOTSTRAP_SERVERS_KEY)) {
        // Bootstrap servers aren't provided by user. Fetch bootstrap servers from the framework.
        LOGGER.info("{} not provided in env, querying framework for broker list.",
            ENV_KAFKA_BOOTSTRAP_SERVERS_KEY);
        BrokerLookup serverLookup = new BrokerLookup(startupConfig.kafkaFrameworkName);

        List<String> bootstrapServers;
        try {
          bootstrapServers = serverLookup.getBootstrapServers();
        } catch (IOException e) {
          LOGGER.error("Failed to retrieve brokers from Kafka framework", e);
          return null;
        }
        StringBuilder brokerHostsStrBuilder = new StringBuilder();
        for (String endpoint : bootstrapServers) {
          brokerHostsStrBuilder.append(endpoint).append(',');
        }
        if (brokerHostsStrBuilder.length() > 0) {
          brokerHostsStrBuilder.deleteCharAt(brokerHostsStrBuilder.length() - 1);
          kafkaConfig.put(KAFKA_BOOTSTRAP_SERVERS_KEY, brokerHostsStrBuilder.toString());
        }
      }

      return new KafkaConfig(kafkaConfig);
    }

    private KafkaConfig(Map<String, Object> kafkaConfig) {
      this.kafkaConfig = kafkaConfig;
    }
  }


  /**
   * Local hack to provide a bridge between get() and printFlagParseFailure().
   */
  private static String lastGetKey = "";
  private static String lastGetValue = "";

  private static void printFlagParseFailure(Throwable e) {
    LOGGER.error(String.format("Failed to parse value for arg %s=%s", lastGetKey, lastGetValue), e);
  }
}
