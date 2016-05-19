package com.mesosphere.metrics.consumer;

import java.io.IOException;
import java.net.URISyntaxException;
import org.kairosdb.client.HttpClient;
import org.kairosdb.client.builder.Metric;
import org.kairosdb.client.builder.MetricBuilder;

import com.mesosphere.metrics.consumer.common.ConsumerRunner;
import com.mesosphere.metrics.consumer.common.MetricOutput;

import dcos.metrics.Datapoint;
import dcos.metrics.MetricList;
import dcos.metrics.Tag;

public class KairosMain {
  private static class KairosOutput implements MetricOutput {
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
        client.pushMetrics(metricBuilder);
      } catch (URISyntaxException | IOException e) {
        e.printStackTrace();
        if (exitOnConnectFailure) {
          System.exit(1);
        }
      }
      metricBuilder = MetricBuilder.getInstance();
    }
  }

  private static boolean parseBool(String str, boolean defaultVal) {
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

  public static void main(String[] args) throws IOException {
    String hostFlag = System.getenv("OUTPUT_HOST");
    String portFlag = System.getenv("OUTPUT_PORT");
    if (hostFlag == null || portFlag == null) {
      throw new IllegalArgumentException("OUTPUT_HOST and OUTPUT_PORT are required");
    }
    boolean exitOnConnectFailure = parseBool(System.getenv("EXIT_ON_CONNECT_FAILURE"), true);
    ConsumerRunner.run(new KairosOutput(
        hostFlag, Integer.parseInt(portFlag), exitOnConnectFailure));
  }
}
