package com.mesosphere.metrics.consumer.common;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map.Entry;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Set;

/**
 * A set of static methods for use with retrieving and parsing commandline arguments.
 */
public class ArgUtils {

  private static final Logger LOGGER = LoggerFactory.getLogger(ArgUtils.class);

  private ArgUtils() {
    // do not instantiate
  }

  /**
   * Prints the names and values of all environment variables. The values of {@code maskedArgs} will
   * not be displayed and will instead be automatically replaced with 4 asterisks.
   */
  public static void printArgs(String... maskedArgs) {
    Set<String> maskedArgsUpper = new HashSet<String>();
    for (String s : maskedArgs) {
      maskedArgsUpper.add(s.toUpperCase());
    }
    Set<Entry<String, String>> envvars = System.getenv().entrySet();
    LOGGER.info("# Start environment: {} entries", envvars.size());
    for (Entry<String, String> entry : System.getenv().entrySet()) {
      if (maskedArgsUpper.contains(entry.getKey().toUpperCase())) {
        LOGGER.info("%s=****", entry.getKey());
      } else {
        LOGGER.info("{}={}", entry.getKey(), entry.getValue());
      }
    }
    LOGGER.info("# End environment: {} entries", envvars.size());
  }

  /**
   * Returns the environment variable named {@code envName}, or returns {@code defaultVal} if the
   * environment variable is missing or empty.
   */
  public static String parseStr(String envName, String defaultVal) {
    String str = System.getenv(envName);
    if (str == null || str.isEmpty()) {
      return defaultVal;
    }
    return str;
  }

  /**
   * Returns the environment variable named {@code envName}, or throws an
   * {@link IllegalArgumentException} if the environment variable is missing or empty.
   */
  public static String parseRequiredStr(String envName) {
    String str = System.getenv(envName);
    if (str == null || str.isEmpty()) {
      throw new IllegalArgumentException(envName + " is required");
    }
    return str;
  }

  /**
   * Returns a boolean representation of the environment variable named {@code envName}, or returns
   * {@code defaultVal} if the environment variable is missing or empty.
   */
  public static boolean parseBool(String envName, boolean defaultVal) {
    return toBool(parseStr(envName, String.valueOf(defaultVal)));
  }

  /**
   * Returns a boolean representation of the environment variable named {@code envName}, or throws
   * an {@link IllegalArgumentException} if the environment variable is missing or empty.
   */
  public static boolean parseRequiredBool(String envName) {
    return toBool(parseRequiredStr(envName));
  }

  /**
   * Returns an integer representation of the environment variable named {@code envName}, or returns
   * {@code defaultVal} if the environment variable is missing or empty.
   */
  public static int parseInt(String envName, int defaultVal) {
    return Integer.valueOf(parseStr(envName, String.valueOf(defaultVal)));
  }

  /**
   * Returns an integer representation of the environment variable named {@code envName}, or throws
   * an {@link IllegalArgumentException} if the environment variable is missing or empty.
   */
  public static int parseRequiredInt(String envName) {
    return Integer.valueOf(parseRequiredStr(envName));
  }

  /**
   * Returns a long integer representation of the environment variable named {@code envName}, or
   * returns {@code defaultVal} if the environment variable is missing or empty.
   */
  public static long parseLong(String envName, int defaultVal) {
    return Long.valueOf(parseStr(envName, String.valueOf(defaultVal)));
  }

  /**
   * Returns a long integer representation of the environment variable named {@code envName}, or
   * throws an {@link IllegalArgumentException} if the environment variable is missing or empty.
   */
  public static long parseRequiredLong(String envName) {
    return Long.valueOf(parseRequiredStr(envName));
  }

  /**
   * Returns a list of strings extracted from the comma-separated environment variable named
   * {@code envName}, or returns an empty list if the environment variable is missing or empty.
   */
  public static List<String> parseStrList(String envName) {
    String arg = parseStr(envName, "");
    if (arg.isEmpty()) { // avoid returning [""]
      return new ArrayList<>();
    }
    return Arrays.asList(arg.split(","));
  }

  /**
   * Returns a boolean representation of the provided string
   * @param envVal a non-empty string
   */
  private static boolean toBool(String str) {
    switch (str.charAt(0)) {
    case 't':
    case 'T':
    case '1':
      return true;
    default:
      return false;
    }
  }

}
