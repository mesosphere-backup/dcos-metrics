package com.mesosphere.metrics.consumer.common;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

public class Stats {
  public static class Values {

    private static final Logger LOGGER = LoggerFactory.getLogger(Values.class);

    private static class Result {
      private long total;
      private long sinceLastGet;

      private Result(long total, long sinceLastGet) {
        this.total = total;
        this.sinceLastGet = sinceLastGet;
      }
    }

    private static class LongCounter {

      private final AtomicLong val;
      private long lastVal;

      private LongCounter() {
        val = new AtomicLong(0);
        lastVal = 0;
      }

      private Result getResult() {
        long cur = val.get();
        long sinceLastGet = cur - lastVal;
        lastVal = cur;
        return new Result(cur, sinceLastGet);
      }
    }

    private final AtomicBoolean fatalError = new AtomicBoolean(false);
    private final AtomicBoolean shutdown = new AtomicBoolean(false);
    private final LongCounter records = new LongCounter();
    private final LongCounter bytes = new LongCounter();
    private final LongCounter datapoints = new LongCounter();
    private final LongCounter tags = new LongCounter();
    private final LongCounter metricLists = new LongCounter();
    private final LongCounter errors = new LongCounter();

    private final long startTimeNano;
    private long lastPrintTimeNano;

    public Values() {
      startTimeNano = System.nanoTime();
      lastPrintTimeNano = startTimeNano;
    }

    public void registerError(Throwable e) {
      errors.val.getAndIncrement();
      LOGGER.error("ERROR", e);
    }

    public void incRecords(long count) {
      records.val.getAndAdd(count);
    }

    public void incBytes(long count) {
      bytes.val.getAndAdd(count);
    }

    public void incDatapoints(long count) {
      datapoints.val.getAndAdd(count);
    }

    public void incTags(long count) {
      tags.val.getAndAdd(count);
    }

    public void incMetricLists(long count) {
      metricLists.val.getAndAdd(count);
    }

    public void setFatalError(Throwable e) {
      registerError(e);
      fatalError.set(true);
      setShutdown();
    }

    public boolean isFatalError() {
      return fatalError.get();
    }

    public void setShutdown() {
      shutdown.set(true);
    }

    public boolean isShutdown() {
      return shutdown.get();
    }

    private void emit() {
      long timeNanoNow = System.nanoTime();

      Result recordsResult = records.getResult();
      Result bytesResult = bytes.getResult();
      Result errorsResult = errors.getResult();

      Result metricListsResult = metricLists.getResult();
      Result datapointsResult = datapoints.getResult();
      Result tagsResult = tags.getResult();

      double timeSinceLastPrintSecs = (timeNanoNow - lastPrintTimeNano) / (double) 1000000000.;
      double timeSinceStartSecs = (timeNanoNow - startTimeNano) / (double) 1000000000.;
      lastPrintTimeNano = timeNanoNow;

      LOGGER.info("STATS RECENT: {} records, {} records/sec, {} bytes, {} bytes/sec, {} errors, {} errors/sec",
          recordsResult.sinceLastGet, recordsResult.sinceLastGet / timeSinceLastPrintSecs,
          bytesResult.sinceLastGet, bytesResult.sinceLastGet / timeSinceLastPrintSecs,
          errorsResult.sinceLastGet, errorsResult.sinceLastGet / timeSinceLastPrintSecs);
      LOGGER.info("STATS RECENT: {} metriclists, {} metriclists/sec, {} datapoints, {} datapoints/sec, {} tags, {} tags/sec",
          metricListsResult.sinceLastGet, metricListsResult.sinceLastGet / timeSinceLastPrintSecs,
          datapointsResult.sinceLastGet, datapointsResult.sinceLastGet / timeSinceLastPrintSecs,
          tagsResult.sinceLastGet, tagsResult.sinceLastGet / timeSinceLastPrintSecs);

      LOGGER.info("STATS TOTAL: {} records, {} records/sec, {} bytes, {} bytes/sec, {} errors, {} errors/sec",
          recordsResult.total, recordsResult.total / timeSinceStartSecs,
          bytesResult.total, bytesResult.total / timeSinceStartSecs,
          errorsResult.total, errorsResult.total / timeSinceStartSecs);
      LOGGER.info("STATS TOTAL: {} metriclists, {} metriclists/sec, {} datapoints, {} datapoints/sec, {} tags, {} tags/sec",
          metricListsResult.total, metricListsResult.total / timeSinceStartSecs,
          datapointsResult.total, datapointsResult.total / timeSinceStartSecs,
          tagsResult.total, tagsResult.total / timeSinceStartSecs);
    }
  }

  public static class PrintRunner implements Runnable {

    private final ClientConfigs.StatsConfig config;
    private final Values values;

    public PrintRunner(ClientConfigs.StatsConfig config) {
      this.config = config;
      this.values = new Values();
    }

    public Values getValues() {
      return values;
    }

    @Override
    public void run() {
      while (!values.isShutdown()) {
        values.emit();
        try {
          Thread.sleep(config.printPeriodMs);
        } catch (InterruptedException e) {
          values.registerError(e);
        }
      }
    }
  }
}
