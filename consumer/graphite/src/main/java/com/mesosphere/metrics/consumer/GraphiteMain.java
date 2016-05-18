package com.mesosphere.metrics.consumer;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.mesosphere.metrics.consumer.common.ConsumerRunner;
import com.mesosphere.metrics.consumer.common.MetricOutput;

import dcos.metrics.MetricList;

public class GraphiteMain {
  private static class GraphiteOutput implements MetricOutput {
    private static final Logger LOGGER = LoggerFactory.getLogger(GraphiteOutput.class);

    public GraphiteOutput(String host, int port) {

    }

    @Override
    public void append(MetricList list) {
      //TODO
    }

    @Override
    public void flush() {
      //TODO
    }
  }

  public static void main(String[] args) {
    String host = System.getenv("OUTPUT_HOST");
    String port = System.getenv("OUTPUT_PORT");
    if (host == null || port == null) {
      throw new IllegalArgumentException("OUTPUT_HOST and OUTPUT_PORT are required");
    }
    ConsumerRunner.run(new GraphiteOutput(host, Integer.parseInt(port)));
  }
}
