package com.mesosphere.metrics.consumer;

import java.io.IOException;

import com.codahale.metrics.graphite.Graphite;
import com.codahale.metrics.graphite.GraphiteRabbitMQ;
import com.codahale.metrics.graphite.GraphiteSender;
import com.codahale.metrics.graphite.GraphiteUDP;
import com.codahale.metrics.graphite.PickledGraphite;
import com.mesosphere.metrics.consumer.common.ArgUtils;
import com.mesosphere.metrics.consumer.common.ConsumerRunner;
import com.mesosphere.metrics.consumer.common.MetricOutput;

import dcos.metrics.Datapoint;
import dcos.metrics.MetricList;
import dcos.metrics.Tag;

public class GraphiteMain {
  private static class GraphiteOutput implements MetricOutput {
    private static final char DELIM = '.';
    private static final String FRAMEWORK_ID = "framework_id", EXECUTOR_ID = "executor_id", CONTAINER_ID = "container_id";

    private final GraphiteSender client;
    private final String prefix;
    private final boolean prefixContainerIds;
    private final boolean exitOnConnectFailure;

    public GraphiteOutput(GraphiteSender client, String prefix, boolean prefixContainerIds, boolean exitOnConnectFailure)
        throws IllegalStateException, IOException {
      this.client = client;
      this.prefix = prefix;
      this.prefixContainerIds = prefixContainerIds;
      this.exitOnConnectFailure = exitOnConnectFailure;
    }

    @Override
    public void append(MetricList list) {
      // graphite doesn't have native tags. as a workaround for this, we include common tags as prefixes to the name.
      StringBuilder prefixBuilder = new StringBuilder();
      if (!prefix.isEmpty()) {
        prefixBuilder.append(prefix);
        prefixBuilder.append(DELIM);
      }
      if (prefixContainerIds) {
        String frameworkId = null, executorId = null, containerId = null;
        for (Tag t : list.getTags()) {
          if (t.getKey().equals(FRAMEWORK_ID)) {
            frameworkId = t.getValue();
          } else if (t.getKey().equals(EXECUTOR_ID)) {
            executorId = t.getValue();
          } else if (t.getKey().equals(CONTAINER_ID)) {
            containerId = t.getValue();
          }
        }
        if (frameworkId != null) {
          prefixBuilder.append(frameworkId);
          prefixBuilder.append(DELIM);
        }
        if (executorId != null) {
          prefixBuilder.append(executorId);
          prefixBuilder.append(DELIM);
        }
        if (containerId != null) {
          prefixBuilder.append(containerId);
          prefixBuilder.append(DELIM);
        }
      }

      try {
        for (Datapoint d : list.getDatapoints()) {
          // automatically connects if not connected
          client.send(prefixBuilder.toString() + d.getName(), d.getValue().toString(), d.getTimeMs());
        }
      } catch (IOException e) {
        e.printStackTrace();
        if (exitOnConnectFailure) {
          System.exit(1);
        }
      }
    }

    @Override
    public void flush() {
      try {
        client.flush();
      } catch (IOException e) {
        e.printStackTrace();
        if (exitOnConnectFailure) {
          System.exit(1);
        }
      }
    }
  }

  private static GraphiteSender getSender(String host, int port) throws Exception {
    String clientType = ArgUtils.parseStr("GRAPHITE_PROTOCOL", "TCP");
    GraphiteSender sender = null;
    if (clientType.equalsIgnoreCase("TCP")) {
      sender = new Graphite(host, port);
    } else if (clientType.equalsIgnoreCase("UDP")) {
      sender = new GraphiteUDP(host, port);
    } else if (clientType.equalsIgnoreCase("PICKLE")) {
      sender = new PickledGraphite(host, port);
    } else if (clientType.equalsIgnoreCase("RABBITMQ")) {
      sender = new GraphiteRabbitMQ(host, port,
          ArgUtils.parseRequiredStr("RABBIT_USERNAME"),
          ArgUtils.parseRequiredStr("RABBIT_PASSWORD"),
          ArgUtils.parseRequiredStr("RABBIT_EXCHANGE"));
    }
    sender.connect();
    return sender;
  }

  public static void main(String[] args) {
    ArgUtils.printArgs("RABBIT_USERNAME", "RABBIT_PASSWORD");

    ConsumerRunner.run(new ConsumerRunner.MetricOutputFactory() {
      @Override
      public MetricOutput getOutput() throws Exception {
        return new GraphiteOutput(
            getSender(
                ArgUtils.parseRequiredStr("OUTPUT_HOST"),
                ArgUtils.parseInt("OUTPUT_PORT", 2003)),
            ArgUtils.parseStr("GRAPHITE_PREFIX", ""),
            ArgUtils.parseBool("GRAPHITE_PREFIX_IDS", true),
            ArgUtils.parseBool("EXIT_ON_CONNECT_FAILURE", true));
      }
    });
  }
}
