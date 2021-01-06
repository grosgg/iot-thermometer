#include "secrets.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
extern "C" {
  #include "libb64/cdecode.h"
}

#define DHTPIN 2     // Data pin for DHT
#define DHTTYPE DHT22   // DHT 22 (AM2302)
#define SEND_INTERVAL 60

DHT dht(DHTPIN, DHTTYPE);

void msgReceived(char* topic, byte* payload, unsigned int len);

WiFiClientSecure wifiClient;
BearSSL::X509List cert(AWS_CERT_CA);
BearSSL::X509List client_crt(AWS_CERT_CRT);
BearSSL::PrivateKey key(AWS_CERT_PRIVATE);

PubSubClient pubSubClient(AWS_IOT_ENDPOINT, 8883, msgReceived, wifiClient); 

unsigned long lastPublish;

void msgReceived(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received on "); Serial.print(topic); Serial.print(": ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void pubSubCheckConnect() {
  if (!pubSubClient.connected()) {
    Serial.print("PubSubClient connecting to: "); Serial.print(AWS_IOT_ENDPOINT);
    while ( ! pubSubClient.connected()) {
      Serial.print(".");
      pubSubClient.connect(THINGNAME);
    }
    Serial.println(" connected");
    pubSubClient.subscribe(AWS_IOT_TOPIC);
  }
  pubSubClient.loop();
}

int b64decode(String b64Text, uint8_t* output) {
  base64_decodestate s;
  base64_init_decodestate(&s);
  int cnt = base64_decode_block(b64Text.c_str(), b64Text.length(), (char*)output, &s);
  return cnt;
}

void setCurrentTime() {
  configTime(9 * 3600, 0, "jp.pool.ntp.org", "time.nist.gov");

  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: "); Serial.print(asctime(&timeinfo));
}

void setup() {
  // Setup Wifi
  Serial.begin(9600); Serial.println();
  Serial.println("ESP8266 AWS IoT Example");

  Serial.print("Connecting to "); Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.waitForConnectResult();
  Serial.print(", WiFi connected, IP address: "); Serial.println(WiFi.localIP());

  // Get current time, otherwise certificates are flagged as expired
  setCurrentTime();

  // Start DHT
  dht.begin();

  // Configure certificates
  Serial.println("Set Certificates");
  wifiClient.setTrustAnchors(&cert);
  wifiClient.setClientRSACert(&client_crt, &key);

}

void loop() {
  pubSubCheckConnect();

  if (millis() - lastPublish > SEND_INTERVAL * 1000) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    
    // Check if any reads failed and exit early (to try again).
    if (isnan(h) || isnan(t)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    StaticJsonDocument<200> doc;
    doc["station_id"] = STATION_ID;
    doc["timestamp"] = time(nullptr);
    doc["humidity"] = h;
    doc["temperature"] = t;
    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer);

    pubSubClient.publish(AWS_IOT_TOPIC, jsonBuffer);
    Serial.print("Published: "); Serial.println(jsonBuffer);
    lastPublish = millis();
  }
}
