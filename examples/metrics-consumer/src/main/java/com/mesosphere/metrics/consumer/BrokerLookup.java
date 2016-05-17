package com.mesosphere.metrics.consumer;

import org.apache.commons.io.IOUtils;
import org.json.JSONArray;
import org.json.JSONObject;
import org.json.JSONTokener;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.URL;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.List;

public class BrokerLookup {
  private static final Logger LOGGER = LoggerFactory.getLogger(BrokerLookup.class);
  private static final String MARATHON_LOOKUP_TEMPLATE = "http://master.mesos:8080/v2/apps/%s";
  private static final String SCHEDULER_CONNECTION_TEMPLATE = "http://%s.mesos:%d/v1/connection";

  private final String frameworkName;

  public BrokerLookup(String frameworkName) {
    this.frameworkName = frameworkName;
  }

  public List<String> getBootstrapServers() throws IOException {

    // First, query marathon to get the scheduler's port
    // (too lazy to figure out SRV record lookup in Java)
    URL url = new URL(String.format(MARATHON_LOOKUP_TEMPLATE, frameworkName));
    LOGGER.info("Connecting to {} for scheduler port lookup", url.toString());
    JSONObject responseObj = getAndLogResponse(url);

    // response["app"]["tasks"][0]["ports"][0] = portnum
    int schedulerPort = responseObj
        .getJSONObject("app")
        .getJSONArray("tasks")
        .getJSONObject(0)
        .getJSONArray("ports")
        .getInt(0);

    url = new URL(String.format(SCHEDULER_CONNECTION_TEMPLATE, frameworkName, schedulerPort));
    LOGGER.info("Connecting to {} for broker lookup", url.toString());
    responseObj = getAndLogResponse(url);

    JSONArray responseBrokerList = responseObj.getJSONArray("dns");
    List<String> brokerList = new ArrayList<>();
    for (int i = 0; i < responseBrokerList.length(); ++i) {
      brokerList.add(responseBrokerList.getString(i));
    }
    if (brokerList.isEmpty()) {
      LOGGER.warn("Response from {} contained an empty broker list", url.toString());
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
      LOGGER.info("Got response: {}", response.trim());
      JSONObject jsonObject = new JSONObject(response);
      LOGGER.info("JSON from response: {}", jsonObject.toString());
      return jsonObject;
    } finally {
      IOUtils.closeQuietly(reader);
    }
  }
}
