package com.mesosphere.metrics.consumer;

import java.io.IOException;
import java.util.concurrent.TimeUnit;

import org.influxdb.InfluxDB;
import org.influxdb.InfluxDBFactory;
import org.influxdb.dto.Point;
import org.influxdb.dto.Pong;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.mesosphere.metrics.consumer.common.ConsumerRunner;
import com.mesosphere.metrics.consumer.common.MetricOutput;

import dcos.metrics.Datapoint;
import dcos.metrics.MetricList;
import dcos.metrics.Tag;

public class InfluxMain {
  private static final Logger LOGGER = LoggerFactory.getLogger(InfluxOutput.class);

  private static class InfluxOutput implements MetricOutput {

    private final InfluxDB client;
    private final String dbName;
    private final String measurementName;
    private final String retentionPolicy;

    public InfluxOutput(
        InfluxDB client, String dbName, String measurementName, String retentionPolicy)
            throws IllegalStateException, IOException {
      this.client = client;
      this.dbName = dbName;
      this.measurementName = measurementName;
      this.retentionPolicy = retentionPolicy;
    }

    @Override
    public void append(MetricList list) {
      // tried using BatchPoints, and it only did lots of this: {"error":"write failed: field overflow"}
      for (Datapoint d : list.getDatapoints()) {
        Point.Builder pointBuilder = Point.measurement(measurementName);
        for (Tag t : list.getTags()) {
          pointBuilder.tag(t.getKey(), t.getValue());
        }
        Point point = pointBuilder
            .addField(d.getName(), d.getValue())
            .time(d.getTimeMs(), TimeUnit.MILLISECONDS)
            .build();
        client.write(dbName, retentionPolicy, point);
      }
    }

    @Override
    public void flush() {
      // already flushed above
    }
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

  private static String parseStr(String envName, String defaultVal) {
    String str = System.getenv(envName);
    if (str == null || str.isEmpty()) {
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

  private static int parseInt(String envName, int defaultVal) {
    String str = System.getenv(envName);
    if (str == null || str.isEmpty()) {
      return defaultVal;
    }
    return Integer.valueOf(str);
  }

  private static InfluxDB.LogLevel parseLogLevel(String envName, InfluxDB.LogLevel defaultVal) {
    String str = System.getenv(envName);
    if (str == null || str.isEmpty()) {
      return defaultVal;
    }
    return InfluxDB.LogLevel.valueOf(str);
  }

  public static void main(String[] args) throws IOException {
    String databaseFlag = parseRequiredStr("OUTPUT_DATABASE");
    int batchPoints = parseInt("BATCH_POINTS", 2000);
    int batchMs = parseInt("BATCH_MS", 100);

    ConsumerRunner.run(new ConsumerRunner.MetricOutputFactory() {
      @Override
      public MetricOutput getOutput() throws Exception {
        InfluxDB client = InfluxDBFactory.connect(
            String.format("http://%s:%s",
                parseRequiredStr("OUTPUT_HOST"), parseInt("OUTPUT_PORT", 8086)),
            parseRequiredStr("OUTPUT_USERNAME"), parseRequiredStr("OUTPUT_PASSWORD"));
        Pong pong = client.ping();
        LOGGER.info("Ping response: " + pong.toString());
        if (parseBool("CREATE_DB", true)) {
          client.createDatabase(databaseFlag);
        }
        if (batchPoints > 0 || batchMs > 0) {
          client.enableBatch(batchPoints, batchMs, TimeUnit.MILLISECONDS);
        }
        client.setLogLevel(parseLogLevel("LOG_LEVEL", InfluxDB.LogLevel.BASIC));
        return new InfluxOutput(client, databaseFlag,
            parseRequiredStr("MEASUREMENT_NAME"),
            parseStr("RETENTION_POLICY", "default"));
      }
    });
  }
}
