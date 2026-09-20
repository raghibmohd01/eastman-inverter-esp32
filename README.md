# Eastman Smart Max 6100 Dashboard - ESP32 

This project reads real-time telemetry data from an **Eastman Smart Max 6100 Inverter** using an ESP32 microcontroller and an RS485-to-TTL module. It then hosts a JSON REST API over WiFi, making it easy to build custom dashboards (like Flutter or Home Assistant) to monitor your solar setup.

## Features
- **Passive Listener:** Decodes 115200 baud streaming data (no Modbus polling required).
- **JSON REST API:** Serves parsed data (Battery Voltage, Solar Power, Load %) on `http://<ESP_IP>/telemetry`.
- **Heartbeat Indicator:** Onboard LED flashes every 2 seconds when valid data is received.

## Hardware Required
- ESP32 Development Board (e.g., DOIT DevKit V1)
- RS485 to TTL Module (Auto-direction)
- Standard 5V USB Wall Charger (for power)

## Wiring Guide
1. **Inverter (RS485 Port)** -> **RS485 Module**
   - Pin 1 (A/D+) -> A
   - Pin 2 (B/D-) -> B
2. **RS485 Module** -> **ESP32**
   - VCC -> VIN (5V)
   - GND -> GND
   - RXD -> Pin 27
   - TXD -> Pin 26

## Setup & Installation
1. **Clone the repository:**
   ```bash
   git clone https://github.com/raghibmohd01/eastman-inverter-esp32.git
   cd eastman-inverter-esp32
   ```

2. **Configure WiFi Credentials:**
   Create a file named `include/secrets.h` and add your WiFi details:
   ```cpp
   #ifndef SECRETS_H
   #define SECRETS_H

   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";

   #endif
   ```
   *(Note: `include/secrets.h` is ignored by Git to keep your passwords safe).*

3. **Build & Upload:**
   Use PlatformIO to upload the code to your ESP32:
   ```bash
   pio run -t upload
   ```

4. **Verify:**
   Open the Serial Monitor at `115200` baud to see the decoded frames, or visit the IP address printed in the console (e.g. `http://192.168.1.100/telemetry`).
