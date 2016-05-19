package com.mesosphere.metrics.consumer.common;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

public class Stats {
  public static class Values {

    private static final Logger LOGGER = LoggerFactory.getLogger(Values.class);

    private final AtomicBoolean fatalError;
    private final AtomicBoolean shutdown;
    private final AtomicLong messages;
    private final AtomicLong bytes;
    private final AtomicLong errors;

    private final long startTimeNano;
    private long lastPrintTimeNano;
    private long lastPrintMessages;
    private long lastPrintBytes;
    private long lastPrintErrors;

    public Values() {
      fatalError = new AtomicBoolean(false);
      shutdown = new AtomicBoolean(false);

      messages = new AtomicLong(0);
      bytes = new AtomicLong(0);
      errors = new AtomicLong(0);

      startTimeNano = System.nanoTime();
      lastPrintTimeNano = startTimeNano;
      lastPrintMessages = 0;
      lastPrintBytes = 0;
      lastPrintErrors = 0;
    }

    public void registerError(Throwable e) {
      errors.getAndIncrement();
      LOGGER.error("ERROR", e);
    }

    public void incMessages(long count) {
      messages.getAndAdd(count);
    }

    public void incBytes(long count) {
      bytes.getAndAdd(count);
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

    private void print() {
      long timeNanoNow = System.nanoTime();
      long messagesTotal = messages.get();
      long bytesTotal = bytes.get();
      long errorsTotal = errors.get();

      double timeSinceLastPrintSecs = (timeNanoNow - lastPrintTimeNano) / (double) 1000000000.;
      double timeSinceStartSecs = (timeNanoNow - startTimeNano) / (double) 1000000000.;
      long messagesSinceLastPrint = messagesTotal - lastPrintMessages;
      long bytesSinceLastPrint = bytesTotal - lastPrintBytes;
      long errorsSinceLastPrint = errorsTotal - lastPrintErrors;

      lastPrintTimeNano = timeNanoNow;
      lastPrintMessages = messagesTotal;
      lastPrintBytes = bytesTotal;
      lastPrintErrors = errorsTotal;

      LOGGER.info("STATS RECENT: {} messages, {} messages/sec, {} bytes, {} bytes/sec, {} errors, {} errors/sec",
        messagesSinceLastPrint, messagesSinceLastPrint / timeSinceLastPrintSecs,
        bytesSinceLastPrint, bytesSinceLastPrint / timeSinceLastPrintSecs,
        errorsSinceLastPrint, errorsSinceLastPrint / timeSinceLastPrintSecs);
      LOGGER.info("STATS TOTAL: {} messages, {} messages/sec, {} bytes, {} bytes/sec, {} errors, {} errors/sec",
        messagesTotal, messagesTotal / timeSinceStartSecs,
        bytesTotal, bytesTotal / timeSinceStartSecs,
        errorsTotal, errorsTotal / timeSinceStartSecs);
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
        values.print();
        try {
          Thread.sleep(config.printPeriodMs);
        } catch (InterruptedException e) {
          values.registerError(e);
        }
      }
    }
  }
}
