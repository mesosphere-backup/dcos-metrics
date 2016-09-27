package com.mesosphere.metrics.consumer;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

import com.mesosphere.metrics.consumer.common.ArgUtils;
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

  public static void main(String[] args) {
    ArgUtils.printArgs();

    ConsumerRunner.run(new ConsumerRunner.MetricOutputFactory() {
      @Override
      public MetricOutput getOutput() throws Exception {
        return new StatsdOutput(
            ArgUtils.parseRequiredStr("OUTPUT_HOST"),
            Integer.parseInt(ArgUtils.parseStr("OUTPUT_PORT", "8125")),
            ArgUtils.parseStr("KEY_PREFIX", ""),
            ArgUtils.parseBool("ENABLE_TAGS", true));
      }
    });
  }
}
