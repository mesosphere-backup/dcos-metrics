package com.mesosphere.metrics.consumer;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

import com.mesosphere.metrics.consumer.common.ConsumerRunner;
import com.mesosphere.metrics.consumer.common.MetricOutput;
import com.timgroup.statsd.NonBlockingStatsDClient;
import com.timgroup.statsd.StatsDClient;

import dcos.metrics.Datapoint;
import dcos.metrics.MetricList;
import dcos.metrics.Tag;

public class StatsdMain {
  private static class StatsdOutput implements MetricOutput {
    private static final char STATSD_TAG_DELIM = ':';
    private static final String[] EMPTY_STR_ARRAY = new String[0];

    private final StatsDClient client;
    private final boolean enableTags;

    public StatsdOutput(String host, int port, String keyPrefix, boolean enableTags)
        throws IllegalStateException, IOException {
      client = new NonBlockingStatsDClient(keyPrefix, host, port);
      this.enableTags = enableTags;
    }

    @Override
    public void append(MetricList list) {
      List<String> tags = new ArrayList<>();
      for (Datapoint d : list.getDatapoints()) {
        if (enableTags) {
          for (Tag t : list.getTags()) {
            tags.add(new StringBuilder()
                .append(t.getKey())
                .append(STATSD_TAG_DELIM)
                .append(t.getValue())
                .toString());
          }
          client.gauge(d.getName(), d.getValue(), tags.toArray(EMPTY_STR_ARRAY));
          tags.clear();
        } else {
          //TODO we could also support including tag vals as prefixes to the metric name, but that
          //     implies allowing the user to select which tags are selected, and in what ordering
          client.gauge(d.getName(), d.getValue());
        }
      }
    }

    @Override
    public void flush() {
      // do nothing
    }
  }

  private static String parseOptionalStr(String envName, String defaultVal) {
    String str = System.getenv(envName);
    if (str == null) {
      return defaultVal;
    }
    return str;
  }

  private static String parseRequiredStr(String envName) {
    String str = System.getenv(envName);
    if (str == null || str.isEmpty()) {
      throw new IllegalArgumentException(envName + " is required");
    }
    return str;
  }

  private static boolean parseBool(String envName, boolean defaultVal) {
    String str = System.getenv(envName);
    if (str == null || str.isEmpty()) {
      return defaultVal;
    }
    switch (str.charAt(0)) {
    case 't':
    case 'T':
    case '1':
      return true;
    default:
      return false;
    }
  }

  public static void main(String[] args) {
    ConsumerRunner.run(new ConsumerRunner.MetricOutputFactory() {
      @Override
      public MetricOutput getOutput() throws Exception {
        return new StatsdOutput(
            parseRequiredStr("OUTPUT_HOST"),
            Integer.parseInt(parseOptionalStr("OUTPUT_PORT", "8125")),
            parseOptionalStr("KEY_PREFIX", ""),
            parseBool("ENABLE_TAGS", true));
      }
    });
  }
}
