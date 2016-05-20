package com.mesosphere.metrics.consumer.common;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Map;
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
    public static final String FRAMEWORK_NAME = "FRAMEWORK_NAME";

    public final String frameworkName;

    /**
     * Returns {@code null} if parsing fails.
     */
    public static StartupConfig parseFrom(Map<String, String> testClientConfig) {
      try {
        String frameworkName = get(testClientConfig, FRAMEWORK_NAME, "kafka");
        return new StartupConfig(frameworkName);
      } catch (Throwable e) {
        printFlagParseFailure(e);
        return null;
      }
    }

    private StartupConfig(String frameworkName) {
      this.frameworkName = frameworkName;
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
    public static StatsConfig parseFrom(Map<String, String> config) {
      try {
        long printPeriodMs = Long.parseLong(get(config, "STATS_PRINT_PERIOD_MS", "5000"));
        return new StatsConfig(printPeriodMs);
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
    public final int threads;
    /** Nullable */
    public final String topicExact;
    /** Nullable */
    public final Pattern topicPattern;
    public final long topicPollPeriodMs;

    /**
     * Returns {@code null} if parsing fails.
     */
    public static ConsumerConfig parseFrom(Map<String, String> config) {
      try {
        long pollTimeoutMs = Long.parseLong(get(config, "POLL_TIMEOUT_MS", "1000"));
        int threads = Integer.parseInt(get(config, "CONSUMER_THREADS", "1"));
        String topicExact = get(config, "TOPIC_EXACT", "");
        if (!topicExact.isEmpty()) {
          // exact mode
          return new ConsumerConfig(pollTimeoutMs, threads, topicExact);
        }
        // regex mode
        Pattern topicPattern = Pattern.compile(get(config, "TOPIC_PATTERN", "metrics-.*"));
        long topicPollPeriodMs = Long.parseLong(get(config, "TOPIC_POLL_PERIOD_MS", "60000"));
        return new ConsumerConfig(pollTimeoutMs, threads, topicPattern, topicPollPeriodMs);
      } catch (Throwable e) {
        printFlagParseFailure(e);
        return null;
      }
    }

    private ConsumerConfig(long pollTimeoutMs, int threads, String topicExact) {
      this.pollTimeoutMs = pollTimeoutMs;
      this.threads = threads;
      this.topicExact = topicExact;
      this.topicPattern = null;
      this.topicPollPeriodMs = 0;
    }

    private ConsumerConfig(long pollTimeoutMs, int threads, Pattern topicPattern, long topicPollPeriodMs) {
      this.pollTimeoutMs = pollTimeoutMs;
      this.threads = threads;
      this.topicExact = null;
      this.topicPattern = topicPattern;
      this.topicPollPeriodMs = topicPollPeriodMs;
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

  private static String get(Map<String, String> config, String key, String defaultVal) {
    lastGetKey = key;
    String configVal = config.get(key);
    String val = (configVal != null) ? configVal : defaultVal;
    lastGetValue = val;
    LOGGER.info(String.format("%s = %s", key, val));
    return val;
  }
}
