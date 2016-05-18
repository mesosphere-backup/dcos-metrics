package com.mesosphere.metrics.consumer.common;

import java.io.IOException;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.Map.Entry;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Handles config management across our code as well as settings to be forwarded to Kafka.
 */
public class ConfigParser {

  private static final Logger LOGGER = LoggerFactory.getLogger(ConfigParser.class);

  /**
   * Umbrella for config object passed to Kafka as well as configs used for Test Clients.
   */
  public static class Config {
    private final Map<String, Object> kafkaConfig;
    private final Map<String, String> envConfig;

    private Config(
        Map<String, Object> kafkaConfig,
        Map<String, String> envConfig) {
      this.kafkaConfig = kafkaConfig;
      this.envConfig = envConfig;
    }

    public Map<String, Object> getKafkaConfig() {
      return kafkaConfig;
    }

    public ClientConfigs.StatsConfig getStatsConfig() {
      return ClientConfigs.StatsConfig.parseFrom(envConfig);
    }

    public ClientConfigs.ConsumerConfig getConsumerConfig() {
      return ClientConfigs.ConsumerConfig.parseFrom(envConfig);
    }
  }

  /**
   * Starts with "KAFKA_OVERRIDE_..." => must be for Kafka
   * The key is translated to lowercase, with underscores converted to periods.
   */
  private static final String KAFKA_OVERRIDE_STARTS_WITH = "KAFKA_OVERRIDE_";

  private static final String KAFKA_BOOTSTRAP_SERVERS_KEY = "bootstrap.servers";
  private static final String ENV_KAFKA_BOOTSTRAP_SERVERS_KEY =
      KAFKA_OVERRIDE_STARTS_WITH + KAFKA_BOOTSTRAP_SERVERS_KEY.toUpperCase().replace('.', '_');

  /**
   * Parses and sorts envvars into Kafka config and Test Client config.
   * Returns {@code null} if parsing fails.
   */
  public static Config getConfig() {
    Map<String, Object> kafkaConfig = new TreeMap<>();

    Map<String, String> testClientConfig = new TreeMap<>();
    for (Entry<String, String> entry : System.getenv().entrySet()) {
      if (entry.getKey().startsWith(KAFKA_OVERRIDE_STARTS_WITH)) {
        String kafkaKey = entry.getKey().substring(KAFKA_OVERRIDE_STARTS_WITH.length(), entry.getKey().length());
        kafkaConfig.put(kafkaKey.replace('_', '.').toLowerCase(), entry.getValue());
      } else {
        testClientConfig.put(entry.getKey(), entry.getValue());
      }
    }

    // special case: get the bootstrap endpoints from the Kafka framework.
    // this can be overridden by providing "KAFKA_OVERRIDE_BOOTSTRAP_SERVERS=..." in env.
    ClientConfigs.StartupConfig startupConfig = ClientConfigs.StartupConfig.parseFrom(testClientConfig);
    if (startupConfig == null) {
      LOGGER.error("Failed to parse startup config, exiting");
      return null;
    }
    if (!kafkaConfig.containsKey(KAFKA_BOOTSTRAP_SERVERS_KEY)) {
      // Bootstrap servers aren't provided by user. Fetch bootstrap servers from the framework.
      LOGGER.info("{} not provided in env, querying framework for broker list.", ENV_KAFKA_BOOTSTRAP_SERVERS_KEY);
      if (startupConfig.frameworkName == null) {
        LOGGER.error(
            "Unable to proceed without {} or {}", ClientConfigs.StartupConfig.FRAMEWORK_NAME, ENV_KAFKA_BOOTSTRAP_SERVERS_KEY);
        return null;
      }
      BrokerLookup serverLookup = new BrokerLookup(startupConfig.frameworkName);

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

    return new Config(kafkaConfig, testClientConfig);
  }
}
