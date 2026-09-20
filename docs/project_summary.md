# Project Summary: Eastman Inverter Dashboard

## The Goal
The objective of this project was to read live performance data (like battery voltage, solar panel power, and AC grid usage) from an **Eastman Smart Max 6100 Inverter** and display it wirelessly on a custom dashboard. To achieve this, we used an **ESP32 microcontroller** connected to the inverter via an **RS485 communication module**.

## The Journey & The Hurdles

### 1. The Modbus Misunderstanding
Initially, we thought the inverter communicated using a standard industrial protocol called **Modbus RTU** at a slow speed of `9600 baud`. We programmed the ESP32 to send "poll" requests (asking the inverter "give me your data") and wait for a response. 
**The Issue:** We received garbled data, fake readings (like 655 Volts), and a lot of connection timeouts. 

### 2. The Signal Echo Problem
Because the RS485 module we used automatically switches between sending and receiving, every time the ESP32 asked for data, the request was "echoed" back into its own receiver. The ESP32 was mistakenly reading its own questions as the inverter's answers!

### 3. The Breakthrough
After reviewing the official documentation and logs for the Eastman Inverter, we made a crucial discovery:
* **No Polling Required:** The inverter doesn't wait to be asked. As soon as it's turned on, it continuously "shouts" its data out to the world in a continuous stream.
* **High Speed:** It talks much faster than we thought—at **115200 baud**.
* **Custom Format:** Instead of standard Modbus, it sends a specific 26-byte package of data, always starting with the signature `0xFFFF`.

## The Solution
We completely rewrote the ESP32 code to act as a **passive listener**:
1. **Speed Upgrade:** We increased the ESP32's listening speed to 115200 baud.
2. **Listen Only:** We stopped sending requests entirely to prevent the echo problem.
3. **Data Decoding:** We wrote a custom parser that waits for the `0xFFFF` signature, collects exactly 26 bytes of data, and translates the raw hexadecimal numbers into real-world numbers (like `51.6 Volts` and `344 Watts`).
4. **Heartbeat LED:** We programmed the blue LED on the ESP32 to blink slowly (once every 2 seconds) to indicate it is successfully decoding data without flashing frantically.
5. **Power Supply:** We confirmed that a standard OnePlus mobile wall charger (5V, 2A) is perfectly safe and sufficient to power the ESP32 24/7.

## The Final Result
The ESP32 now successfully reads the live data from the inverter and hosts a mini web server on your local network. When you visit `http://192.168.31.127/telemetry`, the ESP32 provides all the live stats in a clean JSON format. 

Finally, you successfully built a beautifully designed **Flutter Mobile App** that automatically fetches this JSON data and displays it on your phone in real-time!
