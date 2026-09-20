# Technical Concepts for Beginners

Welcome to the tech side of the project! If you are new to electronics and software integration, this document breaks down every concept we used to make this system work.

---

### 1. ESP32 Microcontroller
**What it is:** A tiny, inexpensive computer chip that has built-in WiFi and Bluetooth.
**How it applies here:** Think of the ESP32 as the "brain" of the operation. It sits between your inverter and your WiFi network. It reads the raw electrical signals from the inverter, does the math to understand them, and hosts a tiny website so your phone can see the data.

### 2. RS485 Communication
**What it is:** An industrial standard for sending data over wires. Unlike normal USB cables which only work over short distances (a few feet), RS485 can send data over thousands of feet reliably, even in noisy environments like solar power rooms. It uses two wires (A and B) that mirror each other to cancel out electrical noise.
**How it applies here:** The Eastman inverter outputs its data using RS485. The ESP32 cannot read RS485 directly, which is why we used a small green/blue "RS485 to TTL" module to translate the robust industrial signals into gentle 3.3V signals the ESP32 understands.

### 3. UART / Serial Communication
**What it is:** UART stands for Universal Asynchronous Receiver-Transmitter. It’s one of the oldest and simplest ways for two chips to talk to each other. They just agree on a speed, and send data one bit at a time over a single wire.
**How it applies here:** Once the RS485 module translates the signal, it hands it to the ESP32 using UART through the `RX` (Receive) and `TX` (Transmit) pins. 

### 4. Baud Rate
**What it is:** The speed at which devices talk to each other over Serial communication, measured in "bits per second."
**How it applies here:** If two people speak to each other but one speaks incredibly fast and the other expects them to speak slowly, they won't understand each other. Initially, we thought the inverter spoke at `9600 baud`. It turned out it speaks at `115200 baud`. Once we matched the ESP32's listening speed to 115200, the data suddenly made sense!

### 5. Modbus vs. Data Streaming
**What it is:**
* **Modbus** is a "Call and Response" protocol. You have to ask, "Hey Inverter, what is the battery voltage?" and it replies, "It is 51V".
* **Data Streaming** (or Broadcasting) is continuous. The inverter just stands in the corner shouting, "Battery 51V! Solar 300W! Grid 230V!" constantly, whether anyone is listening or not.
**How it applies here:** We spent hours trying to ask the inverter questions (Modbus), which failed. We fixed it by realizing the inverter uses Data Streaming. We just had to sit back and listen.

### 6. Hexadecimal (Hex) & Byte Decoding
**What it is:** Computers process data in 1s and 0s (binary). To make binary easier for humans to read, we group it into "Hexadecimal" chunks (base-16), using numbers `0-9` and letters `A-F`. A "Byte" is a chunk of data.
**How it applies here:** The inverter sends a package of 26 bytes. The first two bytes are always `0xFF 0xFF` (which is `255 255` in standard numbers). This acts as a "Start of Package" marker. 
When the inverter wants to say the voltage is 49.15V, it sends the number `4915` split into two bytes. The ESP32 takes those two bytes, glues them back together using "bit-shifting", and divides by 100 to get `49.15`.

### 7. REST API & JSON
**What it is:** 
* **JSON** (JavaScript Object Notation) is a standard way to format data as text so different programs can read it easily (using `{ "keys": "values" }`). 
* A **REST API** is a URL link you can visit to get this data.
**How it applies here:** The ESP32 hosts a REST API at `http://192.168.31.127/telemetry`. When your Flutter app visits that link, the ESP32 hands over a nicely formatted JSON text file containing all the decoded voltages and currents.

### 8. Flutter App
**What it is:** A framework made by Google for building beautiful mobile apps (iOS and Android) from a single codebase using the Dart programming language.
**How it applies here:** You used Flutter to build the visual dashboard. The app has a timer that quietly visits the ESP32's REST API every few seconds, downloads the latest JSON data, and updates the progress bars and cards on your screen so you can monitor your solar setup from the couch!
