package com.mesosphere.metrics.consumer.common;

import dcos.metrics.MetricList;

public interface MetricOutput {
  public enum Type {
    NONE, PRINT, KAIROSDB, GRAPHITE
  }

  /**
   * Appends a record which was just consumed.
   * The provided {@link MetricList} value will NOT be valid after the call returns.
   */
  public void append(MetricList metrics);

  /**
   * Flushes a set of zero or more records which were just added via append().
   */
  public void flush();
}
