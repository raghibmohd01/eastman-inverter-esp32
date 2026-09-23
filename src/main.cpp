#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "secrets.h"

#include "addons/RTDBHelper.h"
#include "addons/TokenHelper.h"
#include <Firebase_ESP_Client.h>

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
unsigned long lastFirebaseUpdate = 0;

AsyncWebServer server(80);

#define RXD2 27
#define TXD2 26
#define LED_PIN 2 // Built-in blue LED on ESP32 DevKit V1

struct TelemetryData {
  float batteryVoltage;
  float batteryCurrent;
  float pvVoltage;
  float pvCurrent;
  float pvPower;
  float acInputVoltage;
  float acInputFrequency;
  float acOutputVoltage;
  float acOutputFrequency;
  float loadPercentage;
  float pvEnergy;
  unsigned long lastUpdateTime;
  String rawHex;
} telemetry;

const int FRAME_SIZE = 26;
uint8_t buffer[FRAME_SIZE];
int bufferIndex = 0;
bool headerFound = false;

// Watchdog and Networking Config
unsigned long lastActivityTime = 0;
const unsigned long WATCHDOG_TIMEOUT_MS = 5 * 60 * 1000; // 5 minutes

IPAddress local_IP(192, 168, 31, 127);
IPAddress gateway(192, 168, 31, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

void handleTelemetry(AsyncWebServerRequest *request) {
  lastActivityTime = millis(); // Feed the watchdog

  JsonDocument doc;
  bool isFresh = (millis() - telemetry.lastUpdateTime) < 5000;
  doc["status"] = isFresh ? "online" : "stale";
  doc["inverter_connected"] = isFresh;

  JsonObject data = doc["data"].to<JsonObject>();
  data["batteryVoltage"] = telemetry.batteryVoltage;
  data["batteryCurrent"] = telemetry.batteryCurrent;
  data["pvVoltage"] = telemetry.pvVoltage;
  data["pvCurrent"] = telemetry.pvCurrent;
  data["pvPower"] = telemetry.pvPower;
  data["acInputVoltage"] = telemetry.acInputVoltage;
  data["acInputFrequency"] = telemetry.acInputFrequency;
  data["acOutputVoltage"] = telemetry.acOutputVoltage;
  data["acOutputFrequency"] = telemetry.acOutputFrequency;
  data["loadPercentage"] = telemetry.loadPercentage;
  data["pvEnergy"] = telemetry.pvEnergy;
  data["lastUpdateMs"] =
      millis() -
      telemetry.lastUpdateTime; // Or actual timestamp if NTP is configured

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  serializeJson(doc, *response);
  request->send(response);
}

void parseEastmanFrame() {
  // According to official Eastman Solar Smart Max 6100 specification:
  // buffer[0..1] = 0xFFFF (Header)
  // buffer[2..3] = Battery Voltage (scaled by 100, e.g. 0x1333 = 4915
  // -> 49.15V) buffer[4..5] = Battery Current (scaled by 10, e.g. 0x00C8 = 200
  // -> 20.0A) buffer[6..7] = Panel Voltage (e.g. 0x00B4 = 180V) buffer[8..9] =
  // Panel Current (scaled by 10, e.g. 0x012C = 300 -> 30.0A) buffer[10..11] =
  // PV Power (e.g. 0x1388 = 5000W) buffer[12..13] = AC Input Voltage (e.g.
  // 0x00E6 = 230V) buffer[14..15] = AC Input Frequency (scaled by 10, e.g.
  // 0x01F4 = 500 -> 50.0Hz) buffer[16..17] = AC Output Voltage (e.g. 0x00E7 =
  // 231V) buffer[18..19] = AC Output Frequency (scaled by 10, e.g. 0x01F9 = 505
  // -> 50.5Hz) buffer[20..21] = Load Percentage (e.g. 0x0034 = 52%)
  // buffer[22..23] = PV Energy Low
  // buffer[24..25] = PV Energy High

  float battV = ((buffer[2] << 8) | buffer[3]) / 100.0;

  // Sanity check for valid Eastman 24V or 48V solar PCU (18V to 75V)
  if (battV < 18.0 || battV > 75.0) {
    return;
  }

  telemetry.batteryVoltage = battV;
  telemetry.batteryCurrent = ((buffer[4] << 8) | buffer[5]) / 10.0;
  telemetry.pvVoltage = (float)((buffer[6] << 8) | buffer[7]);
  telemetry.pvCurrent = ((buffer[8] << 8) | buffer[9]) / 10.0;
  telemetry.pvPower = (float)((buffer[10] << 8) | buffer[11]);
  telemetry.acInputVoltage = (float)((buffer[12] << 8) | buffer[13]);
  telemetry.acInputFrequency = ((buffer[14] << 8) | buffer[15]) / 10.0;
  telemetry.acOutputVoltage = (float)((buffer[16] << 8) | buffer[17]);
  telemetry.acOutputFrequency = ((buffer[18] << 8) | buffer[19]) / 10.0;
  telemetry.loadPercentage = (float)((buffer[20] << 8) | buffer[21]);
  telemetry.pvEnergy = (float)((buffer[22] << 8) | buffer[23]);
  telemetry.lastUpdateTime = millis();

  String hexDump = "";
  for (int i = 0; i < FRAME_SIZE; i++) {
    char h[4];
    snprintf(h, sizeof(h), "%02X ", buffer[i]);
    hexDump += h;
  }
  telemetry.rawHex = hexDump;
  lastActivityTime = millis(); // Feed watchdog on successful frame

  // Flash onboard blue LED to show successful telemetry frame reception once
  // every 1 minute
  static unsigned long lastFlash = 0;
  if (millis() - lastFlash > 60000) {
    lastFlash = millis();
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
  }

  Serial.println(
      "\n============================================================");
  Serial.println("  🎉🎉🎉 VALID EASTMAN SMART MAX 6100 FRAME DECODED! 🎉🎉🎉");
  Serial.println(
      "============================================================");
  Serial.printf("  Battery:     %.2f V | %.1f A\n", telemetry.batteryVoltage,
                telemetry.batteryCurrent);
  Serial.printf("  Solar PV:    %.1f V | %.1f A | %.0f W\n",
                telemetry.pvVoltage, telemetry.pvCurrent, telemetry.pvPower);
  Serial.printf("  Grid Input:  %.1f V | %.1f Hz\n", telemetry.acInputVoltage,
                telemetry.acInputFrequency);
  Serial.printf("  AC Output:   %.1f V | %.1f Hz\n", telemetry.acOutputVoltage,
                telemetry.acOutputFrequency);
  Serial.printf("  Load:        %.0f %%\n", telemetry.loadPercentage);
  Serial.printf("  Raw Frame:   %s\n", hexDump.c_str());
  Serial.println(
      "============================================================\n");
}

void setup() {
  Serial.begin(115200);

  // Official Eastman Smart Max 6100 Docklight baud rate is 115200, 8-N-1!
  pinMode(TXD2, OUTPUT);
  digitalWrite(TXD2, HIGH);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("\n\n==================================================");
  Serial.println("   EASTMAN SMART MAX 6100 - 115200 BAUD RECEIVER   ");
  Serial.println("==================================================");

  Serial.printf("Connecting to WiFi: %s\n", ssid);
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP Address: http://");
  Serial.println(WiFi.localIP());

  // Firebase Setup
  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_DATABASE_URL;
  auth.user.email = FIREBASE_USER_EMAIL;
  auth.user.password = FIREBASE_USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  server.on("/telemetry", HTTP_GET, handleTelemetry);
  server.begin();
  Serial.println("Web server started at http://" + WiFi.localIP().toString() +
                 "/telemetry");
}

void loop() {
  if (millis() - lastActivityTime > WATCHDOG_TIMEOUT_MS) {
    Serial.println("\n[WATCHDOG] No activity for 5 minutes. Restarting...");
    delay(1000);
    ESP.restart();
  }

  // server.handleClient(); is not needed for AsyncWebServer

  if (Firebase.ready() &&
      (millis() - lastFirebaseUpdate > 2500)) { // 2.5 seconds interval
    lastFirebaseUpdate = millis();
    bool isFresh = (millis() - telemetry.lastUpdateTime) < 5000;

    FirebaseJson json;
    json.set("status", isFresh ? "online" : "stale");
    json.set("inverter_connected", isFresh);
    json.set("data/batteryVoltage", telemetry.batteryVoltage);
    json.set("data/batteryCurrent", telemetry.batteryCurrent);
    json.set("data/pvVoltage", telemetry.pvVoltage);
    json.set("data/pvCurrent", telemetry.pvCurrent);
    json.set("data/pvPower", telemetry.pvPower);
    json.set("data/acInputVoltage", telemetry.acInputVoltage);
    json.set("data/acInputFrequency", telemetry.acInputFrequency);
    json.set("data/acOutputVoltage", telemetry.acOutputVoltage);
    json.set("data/acOutputFrequency", telemetry.acOutputFrequency);
    json.set("data/loadPercentage", telemetry.loadPercentage);
    json.set("data/pvEnergy", telemetry.pvEnergy);
    json.set("data/lastUpdateMs", millis() - telemetry.lastUpdateTime);

    if (Firebase.RTDB.setJSON(&fbdo, "/telemetry/live", &json)) {
      Serial.println("[Firebase] Push successful");
    } else {
      Serial.printf("[Firebase] Push failed: %s\n", fbdo.errorReason().c_str());
    }
  }

  static unsigned long lastByteTime = 0;
  static unsigned long lastDiagTime = 0;
  static int totalBytes = 0;

  if (millis() - lastDiagTime > 3000) {
    lastDiagTime = millis();
    bool isFresh = (millis() - telemetry.lastUpdateTime) < 5000;
    int rxState = digitalRead(RXD2);
    Serial.printf(
        "\n[STATUS 115200] Pin %d (RX): %s | Status: %s | Total bytes: %d\n",
        RXD2, rxState ? "HIGH" : "LOW",
        isFresh ? "ONLINE (DATA STREAMING)" : "WAITING FOR 0xFFFF FRAME",
        totalBytes);
  }

  while (Serial2.available()) {
    uint8_t b = Serial2.read();
    unsigned long now = millis();
    totalBytes++;

    if (now - lastByteTime > 30) {
      Serial.print("\n[RAW 115200] ");
    }
    Serial.printf("%02X ", b);
    lastByteTime = now;

    // Detect 0xFFFF header
    if (!headerFound) {
      if (bufferIndex == 0 && b == 0xFF) {
        buffer[bufferIndex++] = b;
      } else if (bufferIndex == 1 && b == 0xFF) {
        buffer[bufferIndex++] = b;
        headerFound = true;
      } else {
        bufferIndex = (b == 0xFF) ? 1 : 0;
        if (b == 0xFF)
          buffer[0] = 0xFF;
      }
    } else {
      buffer[bufferIndex++] = b;
      if (bufferIndex >= FRAME_SIZE) {
        parseEastmanFrame();
        bufferIndex = 0;
        headerFound = false;
      }
    }
  }

  delay(1);
}
