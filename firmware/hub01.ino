#include <WiFi.h>
#include <PubSubClient.h>
#include "DHTesp.h"

// ── WiFi ──
const char* ssid     = "Wokwi-GUEST";
const char* password = "";

// ── MQTT (Port 1883 for ESP32 TCP) ──
const char* mqtt_server = "broker.emqx.io";
const int   mqtt_port   = 1883;

// ── Topics (Matches Dashboard) ──
#define TOPIC_TEMP   "agriloop-2026/hub01/temp"
#define TOPIC_HUM    "agriloop-2026/hub01/hum"
#define TOPIC_STATUS "agriloop-2026/hub01/status"

DHTesp dht;
#define DHT_PIN 15

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastPublish = 0;

void setup() {
  Serial.begin(115200);
  dht.setup(DHT_PIN, DHTesp::DHT22);
  
  // WiFi Init
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");

  client.setServer(mqtt_server, mqtt_port);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Unique ID is critical so it doesn't kick the dashboard off
    String clientId = "ESP32_Hub01_" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.publish(TOPIC_STATUS, "HUB_01_ONLINE");
    } else {
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastPublish > 3000) { // Publish every 3 seconds
    lastPublish = now;

    TempAndHumidity data = dht.getTempAndHumidity();
    
    if (!isnan(data.temperature) && !isnan(data.humidity)) {
      // Convert to strings
      String tStr = String(data.temperature, 1);
      String hStr = String(data.humidity, 1);

      // Publish to EMQX
      client.publish(TOPIC_TEMP, tStr.c_str());
      client.publish(TOPIC_HUM, hStr.c_str());
      
      Serial.print("Data Sent: T:"); Serial.print(tStr);
      Serial.print(" H:"); Serial.println(hStr);
    }
  }
}