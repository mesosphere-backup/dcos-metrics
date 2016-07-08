package com.mesosphere.metrics.consumer;

import java.io.IOException;
import org.kairosdb.client.HttpClient;
import org.kairosdb.client.builder.Metric;
import org.kairosdb.client.builder.MetricBuilder;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.mesosphere.metrics.consumer.common.ArgUtils;
import com.mesosphere.metrics.consumer.common.ConsumerRunner;
import com.mesosphere.metrics.consumer.common.MetricOutput;

import dcos.metrics.Datapoint;
import dcos.metrics.MetricList;
import dcos.metrics.Tag;

public class KairosMain {
  private static class KairosOutput implements MetricOutput {
    private static final Logger LOGGER = LoggerFactory.getLogger(KairosOutput.class);

    private final HttpClient client;
    private final boolean exitOnConnectFailure;
    private MetricBuilder metricBuilder;

    public KairosOutput(String host, int port, boolean exitOnConnectFailure)
        throws IllegalStateException, IOException {
      client = new HttpClient(String.format("http://%s:%s", host, port));
      this.exitOnConnectFailure = exitOnConnectFailure;
      metricBuilder = MetricBuilder.getInstance();
    }

    @Override
    public void append(MetricList list) {
      // note: we COULD optimize here by grouping the datapoints by metric name across multiple appends (ie Map<String, Metric>).
      // however, we would also need to ensure that we don't have mismatched tags within these groupings.
      // and in practice I suspect we won't see a ton of duplicate metric names between flush()es anyway
      for (Datapoint d : list.getDatapoints()) {
        if (Double.isNaN(d.getValue())) {
          LOGGER.warn("Skipping value {} = NaN, it won't encode for Kairos anyway.", d.getName());
          continue;
        }
        Metric metric = metricBuilder.addMetric(d.getName()).addDataPoint(d.getValue());
        for (Tag t : list.getTags()) {
          metric.addTag(t.getKey(), t.getValue());
        }
      }
    }

    @Override
    public void flush() {
      if (metricBuilder.getMetrics().isEmpty()) {
        return;
      }
      try {
        LOGGER.info("Writing {} metrics to KairosDB", metricBuilder.getMetrics().size());
        client.pushMetrics(metricBuilder);
      } catch (Throwable e) { // may throw runtime exception
        e.printStackTrace();
        if (exitOnConnectFailure) {
          System.exit(1);
        }
      }
      metricBuilder = MetricBuilder.getInstance();
    }
  }

  public static void main(String[] args) {
    ArgUtils.printArgs();

    ConsumerRunner.run(new ConsumerRunner.MetricOutputFactory() {
      @Override
      public MetricOutput getOutput() throws Exception {
        return new KairosOutput(
            ArgUtils.parseRequiredStr("OUTPUT_HOST"),
            ArgUtils.parseRequiredInt("OUTPUT_PORT"),
            ArgUtils.parseBool("EXIT_ON_CONNECT_FAILURE", true));
      }
    });
  }
}
