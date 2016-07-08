package com.mesosphere.metrics.consumer;

import java.io.IOException;
import java.util.concurrent.TimeUnit;

import org.influxdb.InfluxDB;
import org.influxdb.InfluxDBFactory;
import org.influxdb.dto.Point;
import org.influxdb.dto.Pong;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.mesosphere.metrics.consumer.common.ArgUtils;
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

  public static void main(String[] args) throws IOException {
    ArgUtils.printArgs("OUTPUT_USERNAME", "OUTPUT_PASSWORD");

    String databaseFlag = ArgUtils.parseRequiredStr("OUTPUT_DATABASE");
    int batchPoints = ArgUtils.parseInt("BATCH_POINTS", 2000);
    int batchMs = ArgUtils.parseInt("BATCH_MS", 100);

    ConsumerRunner.run(new ConsumerRunner.MetricOutputFactory() {
      @Override
      public MetricOutput getOutput() throws Exception {
        InfluxDB client = InfluxDBFactory.connect(
            String.format("http://%s:%s",
                ArgUtils.parseRequiredStr("OUTPUT_HOST"),
                ArgUtils.parseInt("OUTPUT_PORT", 8086)),
            ArgUtils.parseRequiredStr("OUTPUT_USERNAME"),
            ArgUtils.parseRequiredStr("OUTPUT_PASSWORD"));
        Pong pong = client.ping();
        LOGGER.info("Ping response: " + pong.toString());
        if (ArgUtils.parseBool("CREATE_DB", true)) {
          client.createDatabase(databaseFlag);
        }
        if (batchPoints > 0 || batchMs > 0) {
          client.enableBatch(batchPoints, batchMs, TimeUnit.MILLISECONDS);
        }
        String logLevelStr = ArgUtils.parseStr("LOG_LEVEL", InfluxDB.LogLevel.BASIC.toString());
        client.setLogLevel(InfluxDB.LogLevel.valueOf(logLevelStr));
        return new InfluxOutput(client, databaseFlag,
            ArgUtils.parseRequiredStr("MEASUREMENT_NAME"),
            ArgUtils.parseStr("RETENTION_POLICY", "default"));
      }
    });
  }
}
