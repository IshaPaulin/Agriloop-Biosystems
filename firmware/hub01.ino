#include <WiFi.h>
#include <PubSubClient.h>
#include "DHTesp.h"

// ── WiFi ────────────────────────────────────────
const char* ssid     = "Wokwi-GUEST";
const char* password = "";

// ── MQTT broker (plain TCP — matches dashboard fallback) ──
const char* mqtt_server = "broker.emqx.io";
const int   mqtt_port   = 1883;

// ── Topics ──────────────────────────────────────
#define TOPIC_TEMP   "agriloop-2026/hub01/temp"
#define TOPIC_HUM    "agriloop-2026/hub01/hum"
#define TOPIC_STATUS "agriloop-2026/hub01/status"

// ── DHT ─────────────────────────────────────────
DHTesp dht;
#define DHT_PIN 15

WiFiClient   espClient;
PubSubClient client(espClient);

unsigned long lastPublish  = 0;
unsigned long lastReconnect = 0;
const unsigned long PUBLISH_INTERVAL   = 3000;  // publish every 3s
const unsigned long RECONNECT_INTERVAL = 5000;

// ── Unique client ID ────────────────────────────
String makeClientId() {
  return "hub01-" + String(millis(), HEX) + String(random(0xffff), HEX);
}

// ── WiFi ────────────────────────────────────────
void connectWiFi() {
  Serial.println("\n[WiFi] Connecting...");
  WiFi.begin(ssid, password);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] FAILED — check SSID");
  }
}

// ── MQTT connect (non-blocking attempt) ─────────
bool tryMQTT() {
  String id = makeClientId();
  Serial.print("[MQTT] Connecting as " + id + " ... ");
  if (client.connect(id.c_str())) {
    Serial.println("OK");
    client.publish(TOPIC_STATUS, "BOOT_OK");
    return true;
  }
  Serial.print("FAILED rc=");
  Serial.println(client.state());
  return false;
}

// ── Status string ────────────────────────────────
const char* getStatus(float t, float h) {
  if (t >= 30.0 || h >= 85.0) return "CRITICAL";
  if (t >= 27.0 || h >= 80.0) return "WARNING";
  return "OPTIMAL";
}

// ════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);   // let Serial settle before first print
  Serial.println("=============================");
  Serial.println("  AgriLoop Hub #01 BOOTING  ");
  Serial.println("=============================");

  randomSeed(analogRead(0));

  // DHT init — give it 2 seconds to warm up
  dht.setup(DHT_PIN, DHTesp::DHT22);
  Serial.println("[DHT] Sensor initialized on GPIO 15");
  delay(2000);

  client.setServer(mqtt_server, mqtt_port);
  client.setKeepAlive(20);
  client.setSocketTimeout(10);

  connectWiFi();
  tryMQTT();

  Serial.println("[SYSTEM] Setup complete — entering loop");
}

// ════════════════════════════════════════════════
void loop() {
  // Keep MQTT alive
  if (client.connected()) {
    client.loop();
  } else {
    unsigned long now = millis();
    if (now - lastReconnect > RECONNECT_INTERVAL) {
      lastReconnect = now;
      Serial.println("[MQTT] Disconnected — retrying...");
      tryMQTT();
    }
  }

  // Publish on interval
  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;

    TempAndHumidity data = dht.getTempAndHumidity();
    float t = data.temperature;
    float h = data.humidity;

    // ── Serial Monitor output ──────────────────
    Serial.println("─────────────────────────────");
    if (isnan(t) || isnan(h)) {
      Serial.println("[DHT] Read FAILED — check wiring");
      Serial.println("      Is DHT22 on GPIO 15?");
      return;
    }

    const char* status = getStatus(t, h);

    Serial.print("[DHT] Temp     : "); Serial.print(t, 1); Serial.println(" °C");
    Serial.print("[DHT] Humidity : "); Serial.print(h, 1); Serial.println(" %");
    Serial.print("[DHT] Status   : "); Serial.println(status);

    // ── Publish ───────────────────────────────
    char tempStr[8], humStr[8];
    dtostrf(t, 5, 1, tempStr);
    dtostrf(h, 5, 1, humStr);

    // Trim leading spaces (dtostrf pads to width)
    String tStr = String(tempStr); tStr.trim();
    String hStr = String(humStr);  hStr.trim();

    if (!client.connected()) {
      Serial.println("[MQTT] Not connected — skipping publish");
      return;
    }

    bool tOk = client.publish(TOPIC_TEMP,   tStr.c_str());
    bool hOk = client.publish(TOPIC_HUM,    hStr.c_str());
    bool sOk = client.publish(TOPIC_STATUS, status);

    Serial.print("[MQTT] Publish temp=");  Serial.print(tOk ? "OK" : "FAIL");
    Serial.print("  hum=");               Serial.print(hOk ? "OK" : "FAIL");
    Serial.print("  status=");            Serial.println(sOk ? "OK" : "FAIL");

    if (!tOk || !hOk) {
      Serial.println("[MQTT] Publish failed — buffer full? Reconnecting...");
      client.disconnect();
    }
  }
}