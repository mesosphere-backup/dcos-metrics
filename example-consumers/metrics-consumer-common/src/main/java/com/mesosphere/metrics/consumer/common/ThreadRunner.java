package com.mesosphere.metrics.consumer.common;

import java.util.ArrayList;
import java.util.List;

public class ThreadRunner {
  private final Stats.Values values;
  private final List<Thread> threads;

  public ThreadRunner(Stats.Values values) {
    this.values = values;
    this.threads = new ArrayList<>();
  }

  public void add(String label, Runnable runnable) {
    threads.add(new Thread(null, runnable, label, 0));
  }

  public void runThreads() {
    for (Thread thread : threads) {
      thread.start();
    }
    for (Thread thread : threads) {
      try {
        thread.join();
      } catch (InterruptedException e) {
        values.setFatalError(e);
      }
    }
  }

  public boolean isFatalError() {
    return values.isFatalError();
  }
}
