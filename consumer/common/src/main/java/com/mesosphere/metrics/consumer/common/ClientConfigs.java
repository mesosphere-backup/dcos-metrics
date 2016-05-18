package com.mesosphere.metrics.consumer.common;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Map;


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
        String frameworkName = get(testClientConfig, FRAMEWORK_NAME, null);
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
        long printPeriodMs = Long.parseLong(get(config, "STATS_PRINT_PERIOD_MS", "500"));
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
    public final String topic;

    /**
     * Returns {@code null} if parsing fails.
     */
    public static ConsumerConfig parseFrom(Map<String, String> config) {
      try {
        long pollTimeoutMs = Long.parseLong(get(config, "POLL_TIMEOUT_MS", "1000"));
        int threads = Integer.parseInt(get(config, "CONSUMER_THREADS", "1"));
        String topic = get(config, "TOPIC", "sample_metrics");//TODO support a regex for auto-subscribe
        return new ConsumerConfig(pollTimeoutMs, threads, topic);
      } catch (Throwable e) {
        printFlagParseFailure(e);
        return null;
      }
    }

    private ConsumerConfig(long pollTimeoutMs, int threads, String topic) {
      this.pollTimeoutMs = pollTimeoutMs;
      this.threads = threads;
      this.topic = topic;
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

  private static String get(Map<String, String> testClientConfig, String key, String defaultVal) {
    lastGetKey = key;
    String setVal = testClientConfig.get(key);
    String val = (setVal != null) ? setVal : defaultVal;
    lastGetValue = val;
    return val;
  }
}
