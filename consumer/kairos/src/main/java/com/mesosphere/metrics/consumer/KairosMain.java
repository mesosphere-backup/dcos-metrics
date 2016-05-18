package com.mesosphere.metrics.consumer;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.mesosphere.metrics.consumer.common.ConsumerRunner;
import com.mesosphere.metrics.consumer.common.MetricOutput;

import dcos.metrics.MetricList;

public class KairosMain {
  private static class KairosOutput implements MetricOutput {
    private static final Logger LOGGER = LoggerFactory.getLogger(KairosOutput.class);

    public KairosOutput(String host, int port) {

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
    ConsumerRunner.run(new KairosOutput(host, Integer.parseInt(port)));
  }
}
