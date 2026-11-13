// ECSE3038 Test 2: the bathroom fan.
// Every 10 s: read the DHT22's humidity and POST {"mac", "humidity"} to the API with X-API-Key.
// MQTT on the class broker:
//   ecse3038/<name>/fan/settings  JSON from the API, e.g. {"mode": "auto", "max_humidity": 65.0}
//   ecse3038/<name>/fan/state     on or off, retained, only when the fan changes
//   ecse3038/<name>/status        online, or offline from the last will (retained)
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHTesp.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "secrets.h"                          // #define DEVICE_KEY "..."  (never committed)

#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL 6

// Your server (http, no / at the end) and your broker name.
const char *API_URL = "http://iot-rdobbs.northcentralus.cloudapp.azure.com";
const char *MQTT_NAME = "rasheeddobbs";

const char *BROKER_HOST = "135.232.203.144";
const uint16_t BROKER_PORT = 1883;

const unsigned long WIFI_TIMEOUT_MS = 15000;
const unsigned long RETRY_EVERY_MS = 5000;
const unsigned long SEND_EVERY_MS = 10000;
const int DHT_PIN = 15;
const int FAN_PIN = 2;                        // the LED stands for the fan

DHTesp dht;
WiFiClient net;
PubSubClient mqtt(net);

String statusTopic;
String settingsTopic;
String stateTopic;

unsigned long lastSend = 0;
unsigned long lastAttempt = 0;

bool haveSettings = false;                    // fan stays off until the first settings arrive
String fanMode = "off";
float maxHumidity = NAN;
float lastHumidity = NAN;
String reportedState = "";                    // what we last published on fan/state

bool connectWiFi() {
  Serial.print("Connecting to ");
  Serial.print(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      Serial.println();
      Serial.println("No Wi-Fi after 15 s. Trying again in 5 s.");
      return false;
    }
    delay(250);
    Serial.print(".");
  }
  Serial.println(" connected");
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  return true;
}

// B1: one reading to the API, with this fan's key.
void sendReading(float humidity) {
  char body[96];
  snprintf(body, sizeof(body), "{\"mac\": \"%s\", \"humidity\": %.1f}",
           WiFi.macAddress().c_str(), humidity);

  HTTPClient http;
  http.begin(String(API_URL) + "/readings");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", DEVICE_KEY);
  int status = http.POST(body);

  Serial.print("POST ");
  Serial.print(body);
  Serial.print(" -> ");
  Serial.println(status);
  if (status == 401) {
    Serial.println("No key or unknown key: POST /fans with this MAC, put the key in include/secrets.h.");
  } else if (status == 403) {
    Serial.println("This key belongs to another fan.");
  } else if (status < 0) {
    Serial.println(http.errorToString(status));
  }
  http.end();
}

// B4 and B5: drive the fan LED from the settings, then report a change.
void updateFan() {
  bool fanOn = false;
  if (haveSettings) {
    if (fanMode == "on") {
      fanOn = true;
    } else if (fanMode == "auto") {
      fanOn = !isnan(lastHumidity) && !isnan(maxHumidity) && lastHumidity > maxHumidity;
    }
  }
  digitalWrite(FAN_PIN, fanOn ? HIGH : LOW);

  String state = fanOn ? "on" : "off";
  Serial.print("Mode: ");
  Serial.print(haveSettings ? fanMode : String("(none)"));
  Serial.print("  Max: ");
  Serial.print(maxHumidity);
  Serial.print("  Humidity: ");
  Serial.print(lastHumidity);
  Serial.print("  -> fan ");
  Serial.println(state);

  if (state != reportedState && mqtt.connected()) {
    mqtt.publish(stateTopic.c_str(), state.c_str(), true);
    reportedState = state;
  }
}

// B3: settings arrive as JSON. Anything wrong is refused and the settings stay as they were.
void onMessage(char *topic, byte *payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Got ");
  Serial.print(topic);
  Serial.print(" = ");
  Serial.println(message);

  if (String(topic) != settingsTopic) {
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error) {
    Serial.print("Refused: not JSON (");
    Serial.print(error.c_str());
    Serial.println("). Settings unchanged.");
    return;
  }
  if (!doc["mode"].is<const char *>()) {
    Serial.println("Refused: mode missing. Settings unchanged.");
    return;
  }
  if (!doc["max_humidity"].is<float>() && !doc["max_humidity"].is<int>()) {
    Serial.println("Refused: max_humidity missing. Settings unchanged.");
    return;
  }
  String mode = doc["mode"].as<const char *>();
  if (mode != "auto" && mode != "on" && mode != "off") {
    Serial.println("Refused: mode must be auto, on or off. Settings unchanged.");
    return;
  }

  fanMode = mode;
  maxHumidity = doc["max_humidity"].as<float>();
  haveSettings = true;
  updateFan();                                // react now, not at the next reading
}

// B2: connect with a last will, say online, subscribe to the settings.
bool connectMqtt() {
  String clientId = "esp32-" + String(MQTT_NAME) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("Connecting to broker as ");
  Serial.println(clientId);

  bool ok = mqtt.connect(clientId.c_str(), statusTopic.c_str(), 1, true, "offline");
  if (!ok) {
    Serial.print("Broker said no, state ");
    Serial.println(mqtt.state());
    return false;
  }
  Serial.println("Connected to broker");

  mqtt.publish(statusTopic.c_str(), "online", true);
  mqtt.subscribe(settingsTopic.c_str());
  reportedState = "";                         // a new connection: report the fan again
  return true;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hello from the bathroom fan");
  dht.setup(DHT_PIN, DHTesp::DHT22);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);                 // off until the first settings

  String base = "ecse3038/" + String(MQTT_NAME);
  statusTopic = base + "/status";
  settingsTopic = base + "/fan/settings";
  stateTopic = base + "/fan/state";

  mqtt.setServer(BROKER_HOST, BROKER_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setKeepAlive(15);
  mqtt.setBufferSize(512);

  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastAttempt >= RETRY_EVERY_MS) {
      lastAttempt = millis();
      connectWiFi();
    }
    return;
  }

  if (mqtt.connected()) {
    mqtt.loop();
  } else if (millis() - lastAttempt >= RETRY_EVERY_MS) {
    lastAttempt = millis();
    connectMqtt();
  }

  if (millis() - lastSend < SEND_EVERY_MS) {
    return;
  }
  lastSend = millis();

  float humidity = dht.getHumidity();
  if (isnan(humidity)) {
    Serial.println("No reading from the sensor. Check the three wires.");
    return;
  }
  lastHumidity = humidity;
  sendReading(humidity);
  updateFan();
}