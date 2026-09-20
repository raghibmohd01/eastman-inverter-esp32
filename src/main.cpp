#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

#include "secrets.h"


WebServer server(80);

#define RXD2 27
#define TXD2 26
#define LED_PIN 2  // Built-in blue LED on ESP32 DevKit V1

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

void handleTelemetry() {
    JsonDocument doc;
    bool isFresh = (millis() - telemetry.lastUpdateTime) < 5000;
    doc["status"] = isFresh ? "online" : "stale";
    doc["inverter_connected"] = isFresh;
    
    JsonObject data = doc["data"].to<JsonObject>();
    data["batteryVoltage"]    = telemetry.batteryVoltage;
    data["batteryCurrent"]    = telemetry.batteryCurrent;
    data["pvVoltage"]         = telemetry.pvVoltage;
    data["pvCurrent"]         = telemetry.pvCurrent;
    data["pvPower"]           = telemetry.pvPower;
    data["acInputVoltage"]    = telemetry.acInputVoltage;
    data["acInputFrequency"]  = telemetry.acInputFrequency;
    data["acOutputVoltage"]   = telemetry.acOutputVoltage;
    data["acOutputFrequency"] = telemetry.acOutputFrequency;
    data["loadPercentage"]    = telemetry.loadPercentage;
    data["pvEnergy"]          = telemetry.pvEnergy;
    data["lastUpdateMs"]      = millis() - telemetry.lastUpdateTime;
    data["rawHex"]            = telemetry.rawHex;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void parseEastmanFrame() {
    // According to official Eastman Solar Smart Max 6100 specification:
    // buffer[0..1] = 0xFFFF (Header)
    // buffer[2..3] = Battery Voltage (scaled by 100, e.g. 0x1333 = 4915 -> 49.15V)
    // buffer[4..5] = Battery Current (scaled by 10, e.g. 0x00C8 = 200 -> 20.0A)
    // buffer[6..7] = Panel Voltage (e.g. 0x00B4 = 180V)
    // buffer[8..9] = Panel Current (scaled by 10, e.g. 0x012C = 300 -> 30.0A)
    // buffer[10..11] = PV Power (e.g. 0x1388 = 5000W)
    // buffer[12..13] = AC Input Voltage (e.g. 0x00E6 = 230V)
    // buffer[14..15] = AC Input Frequency (scaled by 10, e.g. 0x01F4 = 500 -> 50.0Hz)
    // buffer[16..17] = AC Output Voltage (e.g. 0x00E7 = 231V)
    // buffer[18..19] = AC Output Frequency (scaled by 10, e.g. 0x01F9 = 505 -> 50.5Hz)
    // buffer[20..21] = Load Percentage (e.g. 0x0034 = 52%)
    // buffer[22..23] = PV Energy Low
    // buffer[24..25] = PV Energy High

    float battV = ((buffer[2] << 8) | buffer[3]) / 100.0;
    
    // Sanity check for valid Eastman 24V or 48V solar PCU (18V to 75V)
    if (battV < 18.0 || battV > 75.0) {
        return;
    }

    telemetry.batteryVoltage    = battV;
    telemetry.batteryCurrent    = ((buffer[4] << 8) | buffer[5]) / 10.0;
    telemetry.pvVoltage         = (float)((buffer[6] << 8) | buffer[7]);
    telemetry.pvCurrent         = ((buffer[8] << 8) | buffer[9]) / 10.0;
    telemetry.pvPower           = (float)((buffer[10] << 8) | buffer[11]);
    telemetry.acInputVoltage    = (float)((buffer[12] << 8) | buffer[13]);
    telemetry.acInputFrequency  = ((buffer[14] << 8) | buffer[15]) / 10.0;
    telemetry.acOutputVoltage   = (float)((buffer[16] << 8) | buffer[17]);
    telemetry.acOutputFrequency = ((buffer[18] << 8) | buffer[19]) / 10.0;
    telemetry.loadPercentage    = (float)((buffer[20] << 8) | buffer[21]);
    telemetry.pvEnergy          = (float)((buffer[22] << 8) | buffer[23]);
    telemetry.lastUpdateTime    = millis();

    String hexDump = "";
    for (int i = 0; i < FRAME_SIZE; i++) {
        char h[4];
        snprintf(h, sizeof(h), "%02X ", buffer[i]);
        hexDump += h;
    }
    telemetry.rawHex = hexDump;

    // Flash onboard blue LED to show successful telemetry frame reception once every 2 seconds
    static unsigned long lastFlash = 0;
    if (millis() - lastFlash > 2000) {
        lastFlash = millis();
        digitalWrite(LED_PIN, HIGH);
        delay(50);
        digitalWrite(LED_PIN, LOW);
    }

    Serial.println("\n============================================================");
    Serial.println("  🎉🎉🎉 VALID EASTMAN SMART MAX 6100 FRAME DECODED! 🎉🎉🎉");
    Serial.println("============================================================");
    Serial.printf("  Battery:     %.2f V | %.1f A\n", telemetry.batteryVoltage, telemetry.batteryCurrent);
    Serial.printf("  Solar PV:    %.1f V | %.1f A | %.0f W\n", telemetry.pvVoltage, telemetry.pvCurrent, telemetry.pvPower);
    Serial.printf("  Grid Input:  %.1f V | %.1f Hz\n", telemetry.acInputVoltage, telemetry.acInputFrequency);
    Serial.printf("  AC Output:   %.1f V | %.1f Hz\n", telemetry.acOutputVoltage, telemetry.acOutputFrequency);
    Serial.printf("  Load:        %.0f %%\n", telemetry.loadPercentage);
    Serial.printf("  Raw Frame:   %s\n", hexDump.c_str());
    Serial.println("============================================================\n");
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
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(400);
        Serial.print(".");
    }
    Serial.println();
    Serial.println("WiFi connected!");
    Serial.print("IP Address: http://");
    Serial.println(WiFi.localIP());

    server.on("/telemetry", handleTelemetry);
    server.begin();
    Serial.println("Web server started at http://" + WiFi.localIP().toString() + "/telemetry");
}

void loop() {
    server.handleClient();

    static unsigned long lastByteTime = 0;
    static unsigned long lastDiagTime = 0;
    static int totalBytes = 0;

    if (millis() - lastDiagTime > 3000) {
        lastDiagTime = millis();
        bool isFresh = (millis() - telemetry.lastUpdateTime) < 5000;
        int rxState = digitalRead(RXD2);
        Serial.printf("\n[STATUS 115200] Pin %d (RX): %s | Status: %s | Total bytes: %d\n",
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
                if (b == 0xFF) buffer[0] = 0xFF;
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
