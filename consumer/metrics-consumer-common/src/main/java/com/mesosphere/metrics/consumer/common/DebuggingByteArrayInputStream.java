package com.mesosphere.metrics.consumer.common;

import java.io.ByteArrayInputStream;

import javax.xml.bind.DatatypeConverter;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * A wrapper around ByteArrayInputStream that allows dumping its current state when there's a problem.
 */
public class DebuggingByteArrayInputStream extends ByteArrayInputStream {

  private static final Logger LOGGER = LoggerFactory.getLogger(DebuggingByteArrayInputStream.class);

  private int lastReadOff = 0;
  private int lastReadLen = 0;
  private int lastReadResult = 0;

  public DebuggingByteArrayInputStream(byte[] buf) {
    super(buf);
  }

  public int read(byte[] b, int off, int len) {
    lastReadOff = off;
    lastReadLen = len;
    lastReadResult = super.read(b, off, len);
    return lastReadResult;
  }

  public void dumpState() {
    LOGGER.warn(String.format("State: count=%d, mark=%d, pos=%d", count, mark, pos));
    LOGGER.warn(String.format("Last ranged read: off=%d len=%d => result=%d", lastReadOff, lastReadLen, lastReadResult));
    if (lastReadResult > 0) {
      byte[] data = new byte[lastReadLen];
      read(data, lastReadOff, lastReadLen);
      LOGGER.warn(DatatypeConverter.printHexBinary(data));
    }
    LOGGER.warn(String.format("Buffer of %d bytes:", buf.length));
    LOGGER.warn(DatatypeConverter.printHexBinary(buf));
  }
}
