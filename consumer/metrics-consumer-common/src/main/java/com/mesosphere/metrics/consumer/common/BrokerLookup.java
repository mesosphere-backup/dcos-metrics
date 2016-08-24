package com.mesosphere.metrics.consumer.common;

import org.apache.commons.io.IOUtils;
import org.json.JSONArray;
import org.json.JSONObject;
import org.json.JSONTokener;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.spotify.dns.DnsSrvResolver;
import com.spotify.dns.DnsSrvResolvers;
import com.spotify.dns.LookupResult;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.URL;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.List;

public class BrokerLookup {
  private static final Logger LOGGER = LoggerFactory.getLogger(BrokerLookup.class);
  private static final String SRV_LOOKUP_HOST_TEMPLATE = "_%s._tcp.marathon.mesos";
  private static final String SCHEDULER_CONNECTION_TEMPLATE = "http://%s:%d/v1/connection";

  private final String frameworkName;

  public BrokerLookup(String frameworkName) {
    this.frameworkName = frameworkName;
  }

  public List<String> getBootstrapServers() throws IOException {
    // First, get the scheduler's ip:port using an SRV lookup
    DnsSrvResolver resolver = DnsSrvResolvers.newBuilder().build();
    String hostName = String.format(SRV_LOOKUP_HOST_TEMPLATE, frameworkName);
    List<LookupResult> results = resolver.resolve(hostName);
    if (results.isEmpty()) {
      throw new IOException(String.format(
          "Didn't find any SRV results for %s. Is a Kafka service named '%s' currently running?",
          hostName, frameworkName));
    }
    LookupResult result = results.get(0);

    URL url = new URL(String.format(SCHEDULER_CONNECTION_TEMPLATE, result.host(), result.port()));
    LOGGER.info("Connecting to {} for broker lookup", url.toString());
    JSONObject responseObj = getAndLogResponse(url);

    JSONArray responseBrokerList = responseObj.getJSONArray("dns");
    List<String> brokerList = new ArrayList<>();
    for (int i = 0; i < responseBrokerList.length(); ++i) {
      brokerList.add(responseBrokerList.getString(i));
    }
    if (brokerList.isEmpty()) {
      LOGGER.warn("Response from {} contained an empty broker list", url.toString());
    } else {
      LOGGER.info("Retrieved brokers: {}", brokerList);
    }
    return brokerList;
  }

  private JSONObject getAndLogResponse(URL url) throws IOException {
    if (!LOGGER.isInfoEnabled()) {
      // Pass response stream directly to JSONTokener, foregoing access to response content
      return new JSONObject(new JSONTokener(url.openStream()));
    }

    // Intercept the response text so that it can be logged
    BufferedReader reader = null;
    try {
      reader = new BufferedReader(new InputStreamReader(url.openStream(), Charset.defaultCharset()));
      StringBuilder responseBuilder = new StringBuilder();
      while (true) {
        String line = reader.readLine();
        if (line == null) {
          break;
        }
        responseBuilder.append(line).append('\n');
      }
      String response = responseBuilder.toString();
      LOGGER.debug("Got response: {}", response.trim());
      JSONObject jsonObject = new JSONObject(response);
      LOGGER.debug("JSON from response: {}", jsonObject.toString());
      return jsonObject;
    } finally {
      IOUtils.closeQuietly(reader);
    }
  }
}
