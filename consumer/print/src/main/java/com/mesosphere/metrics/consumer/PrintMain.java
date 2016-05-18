package com.mesosphere.metrics.consumer;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.mesosphere.metrics.consumer.common.ConsumerRunner;
import com.mesosphere.metrics.consumer.common.MetricOutput;

import dcos.metrics.MetricList;

public class PrintMain {
  private static class PrintOutput implements MetricOutput {
    private static final Logger LOGGER = LoggerFactory.getLogger(PrintOutput.class);

    @Override
    public void append(MetricList list) {
      LOGGER.info("Record: {}", list);
    }

    @Override
    public void flush() {
      // do nothing
    }
  }

  public static void main(String[] args) {
    ConsumerRunner.run(new PrintOutput());
  }
}
