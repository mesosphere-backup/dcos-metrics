package com.mesosphere.metrics.consumer.common;

import java.io.ByteArrayInputStream;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * A wrapper around ByteArrayInputStream that allows dumping its current state when there's a problem.
 */
public class DebuggingByteArrayInputStream extends ByteArrayInputStream {

  private static final Logger LOGGER = LoggerFactory.getLogger(DebuggingByteArrayInputStream.class);

  private Integer lastBufLen = 0;
  private int lastReadOff = 0;
  private int lastReadLen = 0;
  private int lastReadResult = 0;

  public DebuggingByteArrayInputStream(byte[] buf) {
    super(buf);
  }

  public int read(byte[] b, int off, int len) {
    lastBufLen = (b == null) ? null : b.length;
    lastReadOff = off;
    lastReadLen = len;
    lastReadResult = super.read(b, off, len);
    return lastReadResult;
  }

  public void dumpState() {
    LOGGER.warn("----");
    // print info as header
    LOGGER.warn(String.format("State: count=%d, mark=%d, pos=%d", count, mark, pos));
    LOGGER.warn(String.format("Last ranged read: buflen=%d off=%d len=%d => result=%d",
        lastBufLen, lastReadOff, lastReadLen, lastReadResult));

    // print buffer surrounded by header/footer which both specify the length
    LOGGER.warn(String.format("Buffer of %d bytes:\n%s\nEnd buffer of %d bytes",
        buf.length, hexDump(buf), buf.length));

    // print info again as footer
    LOGGER.warn(String.format("State: count=%d, mark=%d, pos=%d", count, mark, pos));
    LOGGER.warn(String.format("Last ranged read: buflen=%d off=%d len=%d => result=%d",
        lastBufLen, lastReadOff, lastReadLen, lastReadResult));
    LOGGER.warn("----");
  }

  /**
   * Returns a representation of the provided buffer which mimics the output of 'hexdump -C <file>'
   */
  private String hexDump(byte[] bytes) {
    if (bytes == null) {
      return "NULL";
    }
    StringBuilder sb = new StringBuilder();
    StringBuilder literal = new StringBuilder(); // for the literal string at the end of each line
    final int rowLen = 16;
    for (int i = 0; i < bytes.length; ++i) {
      if (i == 0) { // print initial line header
        sb.append(String.format("%08x  ", i));
      } else {
        if (i % rowLen == 0) { // end of line. print literal column then start new line
          sb.append(String.format(" |%s|\n%08x  ", literal.toString(), i));
          literal = new StringBuilder();
        } else if (i % (rowLen / 2) == 0) { // middle of line. insert space to break up columns
          sb.append(' ');
        }
      }
      if (bytes[i] >= 32 && bytes[i] <= 126) { // printable
        literal.append((char)bytes[i]);
      } else { // not printable
        literal.append('.');
      }
      sb.append(String.format("%02x ", bytes[i])); // print the byte as hex
    }
    // after all bytes have printed, finish up the last line:
    final int lastRowLen = bytes.length % rowLen;
    if (lastRowLen != 0) {
      // add space to line up the last row's literal column
      if (lastRowLen <= (rowLen / 2)) { // include an additional 'break up' space if needed
        sb.append(' ');
      }
      for (int i = 0; i < rowLen - lastRowLen; ++i) {
        sb.append("   ");
      }
    }
    sb.append(String.format(" |%s|", literal.toString()));
    return sb.toString();
  }
}
