# 3. IoT Devices

Hardware Integration & Firmware
This reference guide details the hardware requirements, authentication methods, and MQTT topics required to connect physical devices to Protonest Connect.

When you need this:
* You are developing firmware for a new device (e.g., ESP32, Raspberry Pi Pico W).
* You need the exact MQTT topic structures and JSON payload formats.
* You are implementing Over-The-Air (OTA) update logic.

## 3.1 Device Requirements

This section outlines the hardware specifications, software environment, library dependencies, and network configurations required to successfully connect a device to the Protonest ecosystem.

### 1. Hardware Requirements

Protonest relies on secure communication standards (TLS 1.2), which require specific processing power and memory.

* **Microcontroller Architecture:** Espressif **ESP32** (All Series).
  * **Recommended:** ESP32-WROOM-32 or ESP32-WROVER (Dual Core).
  * **Compatible:** ESP32-S2, ESP32-C3, ESP32-S3.
  * *Note: The ESP8266 is generally not recommended for this specific implementation due to RAM constraints when handling heavy TLS handshakes alongside JSON parsing.*


* **Flash Memory:** Minimum **4 MB** (Required for OTA A/B Partitioning).
* **Connectivity:** Integrated Wi-Fi (802.11 b/g/n).

### 2. Software & Development Environment

The code provided in this documentation is compatible with C++ based frameworks. You must use one of the following environments:

**A. Arduino IDE**

* **Version:** 2.x (Recommended).
* **Board Manager:** `esp32` by Espressif Systems (Version 3.x).

**B. PlatformIO (VS Code)**

* **Platform:** `espressif32`
* **Framework:** `arduino`

### 3. Required Libraries

You must install specific libraries to handle Secure MQTT, JSON serialization, and Over-The-Air updates.

| Library Name | Minimum Version | Purpose |
| --- | --- | --- |
| **WiFi** | Built-in | Manages network connectivity (Station Mode). |
| **WiFiClientSecure** | Built-in | Handles TLS/SSL encryption for secure handshakes (X.509/PSK). |
| **PubSubClient** | v2.8.0 | Lightweight MQTT client for publishing telemetry and subscribing to commands. |
| **ArduinoJson** | v7.4.2 | Parses incoming command payloads and formats outgoing data streams. |
| **HTTPUpdate** | Built-in | Manages the download and writing of firmware binaries during OTA. |
| **LittleFS** | Built-in | File system driver to read certificates (`.pem`, `.crt`) from flash memory. |

### 4. Network Requirements

The deployment environment must meet the following criteria to ensure connectivity.

* **Frequency:** **2.4 GHz** Wi-Fi (5 GHz is not supported by standard ESP32 hardware).
* **Security:** WPA/WPA2 Personal (PSK).
  * *Note: Enterprise security (802.1x/Eduroam) requires additional certificate configuration not covered in this standard guide.*


* **Firewall Ports (Outbound):**
* **TCP 8883:** Secure MQTT traffic (Telemetry Data & Commands).
* **TCP 443:** HTTPS traffic (Fetching OTA Firmware Binaries).



### 5. Partition Scheme (Crucial for OTA)

Over-The-Air (OTA) updates require a specific memory layout. The ESP32 uses an **A/B Partition Scheme**.

* **How it works:** The flash memory is divided into two "App" slots (App0 and App1). When an update is received, it is downloaded to the non-active slot. On a successful download, the device reboots and switches the active slot.
* **The Constraint:** This means your flash memory must be able to hold **two copies** of your firmware at once, plus the file system (SPIFFS/LittleFS).
* **Requirement:** You must select a "Large App" or "Huge App" partition scheme to accommodate the TLS libraries.

**Configuration Instructions:**

**A. Arduino IDE**

1. Go to **Tools** > **Partition Scheme**.
2. Select **Huge APP (3MB No OTA/1MB SPIFFS)** or **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)**.
   * *Note: Ensure the scheme you select has two "app" partitions defined in its definition file if you intend to use OTA.*



**B. PlatformIO (`platformio.ini`)**
Add the following line to your `platformio.ini` file to load a custom partition table that maximizes app space.

```ini
; Use the Huge App partition scheme
board_build.partitions = huge_app.csv

```

## 3.2 Authentication Setup

Before your device can communicate with Protonest, it must prove its identity. You need one of the following credential types, which can be downloaded from your device page on the Protonest Dashboard.

### Credential Types

1. **X.509 Certificates (Recommended):** Uses a `public/private` key pair (`device-cert.pem`, `device-key.pem`) and a server validator (`root-ca.crt`). This is the industry standard for secure IoT.
2. **Pre-Shared Key (PSK):** Uses Username and Password (from `device-creds.txt`) for MQTT authentication and `root-ca.crt` for server authentication

### Prerequisite: Storing Certificates

To use these files, they must be accessible by your device's code. There are two primary methods to achieve this:

* **Method A: Flash Filesystem (Recommended)**
Upload the files directly to the device's storage (LittleFS/SPIFFS). This keeps your code clean and secure.
  * **Arduino IDE:** [How to Upload Files to ESP32 Filesystem](https://)
  * **PlatformIO:** [Upload Filesystem Image Task](https://www)


* **Method B: Hardcoding**
Paste the certificate content directly into your code as variables.
  * **Arduino:** [Hardcoding Certificates Guide](https:)
  * **PlatformIO:** [Hardcoding Certificates Guide](https:)



---

### Authentication Method 01: X.509 Certificates (Recommended)

This method provides the highest level of security. The downloaded credentials package contains three files:

* `root-ca.crt`: Verifies the authenticity of the Protonest server.
* `device-cert.pem`: The public certificate that identifies your specific device.
* `device-key.pem`: The private key that your device uses to sign requests. **Keep this secret.**

**Setup:**

1. Upload all three files to your device's flash drive (refer to the prerequisites above).
2. Use the following code to load and apply them.

**X.509 Sample Code (Arduino/PlatformIO):**
<a id="3_2_X509_Auth_example"></a>

```cpp
// --- INCLUDES ---
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>

// --- CONFIGURATION ---
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASS";

// --- CERTIFICATES ---
const char* ROOT_CA_PATH = "/root-ca.crt";
const char* CLIENT_CERT_PATH = "/device-cert.pem";
const char* CLIENT_KEY_PATH = "/device-key.pem";

// --- OBJECTS ---
WiFiClientSecure net;

// --- FUNCTIONS ---
void connectWifi() {
  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected.");
}

void SyncTime() {
  // Sync Time (CRITICAL)
  // If the device time is incorrect, the server's certificate will be rejected.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Syncing Time");
  while (time(nullptr) < 1000000000l) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nTime Synced.");
}

char* readFile(const char* path) {
  // Read File from Flash
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.printf("Failed to open %s\n", path);
    return "";
  }
  size_t size = file.size();
  char* buf = (char*)malloc(size + 1);
  if (!buf) {
    Serial.println("Malloc failed");
    return nullptr;
  }
  file.readBytes(buf, size);
  buf[size] = '\0'; // Null-terminate the string
  file.close();
  Serial.printf("Loaded %s\n", path);
  return buf;
}

void loadCertificates() {
  // Load certificates
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return;
  }
  // Load Root CA (Verifies Protonest Server)
  char* ca = readFile(ROOT_CA_PATH);
  if (ca[0] == '\0') {
    Serial.println("CA certificate load failed");
    return;
  } else {
    net.setCACert(ca);
  }
  // Load Device Certificate
  char* cert = readFile(CLIENT_CERT_PATH);
  if (cert[0] == '\0') {
    Serial.println("Client certificate load failed");
    return;
  } else {
    net.setCertificate(cert);
  }
  // Load Private Key
  char* key = readFile(CLIENT_KEY_PATH);
  if (key[0] == '\0') {
    Serial.println("Key certificate load failed");
    return;
  } else {
    net.setPrivateKey(key);
  }
}

// --- SETUP ---
void setup() {
  Serial.begin(115200); // Start Serial Debuging
  connectWifi();       // Connect to WiFi
  SyncTime();          // Sync Time (CRITICAL for host validation)
  loadCertificates();  // Load Certs

  // The 'net' object is now ready for secure connections.
}

// --- LOOP ---
void loop() {
  // Main application logic
}

```

**Code Explanation:**

### 1. `// --- INCLUDES ---`

These libraries provide the essential functionality for the device.

* `#include <Arduino.h>`: Standard library for Arduino.
* `#include <WiFi.h>`: The standard library for ESP32 to handle WiFi connections (scanning, connecting, IP management).
* `#include <WiFiClientSecure.h>`: An extension of the WiFi library that adds **TLS/SSL encryption**. This is strictly required for X.509 authentication to encrypt the data stream.
* `#include <LittleFS.h>`: A lightweight filesystem library. It allows the code to read files (like your certificates) stored in the ESP32's flash memory.

### 2. `// --- CONFIGURATION ---`

This section defines the network credentials.

* `WIFI_SSID` & `WIFI_PASS`: Constants storing your WiFi network name and password.

### 3. `// --- CERTIFICATES ---`

These variables define the **file paths** inside the LittleFS partition.

* **`ROOT_CA_PATH`**: Points to the server's public certificate.
* **`CLIENT_CERT_PATH`**: Points to the device's public certificate.
* **`CLIENT_KEY_PATH`**: Points to the device's private key.

### 4. `// --- OBJECTS ---`

* `WiFiClientSecure net;`: Instantiates the secure client object. This object will hold the loaded certificates and manage the encryption layer (handshake, encryption, decryption) for the connection.

### 5. `// --- FUNCTIONS ---`

#### `connectWifi()`

* **Purpose:** Establishes a standard connection to the local network.
* **Logic:** It calls `WiFi.begin()` and enters a `while` loop, checking `WiFi.status()` every 500ms until it receives the `WL_CONNECTED` signal.

#### `SyncTime()`

* **Purpose:** Synchronizes the internal clock via NTP (Network Time Protocol).
* **Why it is Critical:** X.509 Certificates have "Valid From" and "Valid To" timestamps. If the ESP32 thinks the date is Jan 1, 1970 (default boot time), it will reject the server's certificate as "not yet valid."
* **Logic:** It connects to `pool.ntp.org` and waits until the system time is greater than a set epoch threshold (ensuring time was updated).

#### `readFile(const char* path)`

* **Purpose:** A helper function to read a file from storage into the device's RAM.
* **Key Steps:**
  1. **`LittleFS.open(path, "r")`**: Opens the file in read-only mode.
  2. **`malloc(size + 1)`**: Dynamically allocates memory based on the file size. The `+1` reserves space for a Null Terminator.
  3. **`buf[size] = '\0'`**: Manually adds a Null Terminator at the end of the data. This converts the raw file data into a **C-String**, which is the required format for the `WiFiClientSecure` library.

#### `loadCertificates()`

* **Purpose:** Handles the certificate loading process.
* **Logic:**
  1. **`LittleFS.begin(true)`**: Mounts the file system. The `true` parameter formats the drive if it fails to mount (useful for first-time setup).
  2. **Read & Set:** It calls `readFile()` for the CA, Cert, and Key.
  3. **Validation:** Checks if the returned buffer is empty (`\0`).
  4. **Injection:** If valid, it passes the data to the `net` object using:
     * `net.setCACert(ca)`: Loads server validation.
     * `net.setCertificate(cert)`: Loads device identity.
     * `net.setPrivateKey(key)`: Loads the cryptographic signature.

### 6. `// --- SETUP ---`

`setup():` The standard Arduino initialization function that runs once at boot.

1. **`Serial.begin(115200)`**: Starts the serial monitor for debugging output.
2. **`connectWifi()`**: Gets internet access.
3. **`SyncTime()`**: Ensures the clock is correct **before** attempting any secure handshake.
4. **`loadCertificates()`**: Reads files and prepares the secure client.

At the end of `setup()`, the `net` object is fully configured and ready to be passed to an MQTT client (like `PubSubClient`) to establish a secure connection.

---

### Authentication Method 02: Pre-Shared Key (PSK)

This method is simpler but less flexible than X.509. It relies on a static "Username" and "Password" combination alongside a Root CA for encryption. The downloaded package contains:

* `credentials.txt`: Contains the specific **Username** and **Password** for MQTT connection.
* `root-ca.crt`: Used to encrypt the connection to the server.

**Setup:**

1. Upload `root-ca.crt` to your device's flash drive.
2. Open `credentials.txt` and note the Username and Password. We will use it in the next MQTT connection setup section. 

**PSK Sample Code (Arduino/PlatformIO):**
<a id="3_2_PSK_Auth_example"></a>

```cpp
// --- INCLUDES ---
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>

// --- CONFIGURATION ---
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASS";    

// --- CERTIFICATES ---
const char* ROOT_CA_PATH = "/root-ca.crt"; 

// --- OBJECTS ---
WiFiClientSecure net;

// --- FUNCTIONS ---
void connectWifi() {
  //Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected.");
}

void SyncTime() {
  // Sync Time (CRITICAL)
  // If the device time is incorrect, the server's certificate will be rejected.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Syncing Time");
  while (time(nullptr) < 1000000000l) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nTime Synced.");
}

char* readFile(const char* path) {
  // Read File from Flash
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.printf("Failed to open %s\n", path);
    return "";
  }
  size_t size = file.size();
  char* buf = (char*)malloc(size + 1);
  if (!buf) {
    Serial.println("Malloc failed");
    return nullptr;
  }
  file.readBytes(buf, size);
  buf[size] = '\0'; // Null-terminate the string
  file.close();
  Serial.printf("Loaded %s\n", path);
  return buf;
}

void loadCertificates() {
  // Load certificates
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return;
  }
  // Load Root CA (Verifies Protonest Server)
  char* caCert = readFile(ROOT_CA);
  if (caCert[0] == '\0') {
    Serial.println("CA certificate load failed");
    return;
  }
  net.setCACert(caCert);
}

// --- SETUP ---
void setup() {
  Serial.begin(115200); // Start Serial Debuging
  connectWifi();       // Connect to WiFi
  SyncTime();          // Sync Time (CRITICAL for host validation)
  loadCertificates();  // Load Certs

  // The 'net' object is now ready for secure connections.
}

// --- LOOP ---
void loop() {
  // Main application logic
}

```

### Code Explanation:

### 1. `// --- INCLUDES ---`

These libraries provide the essential functionality for the device.

* `#include <Arduino.h>`: Standard library for Arduino.
* `#include <WiFi.h>`: The standard library for ESP32 to handle WiFi connections (scanning, connecting, IP management).
* `#include <WiFiClientSecure.h>`: An extension of the WiFi library that adds **TLS/SSL encryption**. This is strictly required for X.509 authentication to encrypt the data stream.
* `#include <LittleFS.h>`: A lightweight filesystem library. It allows the code to read files (like your certificates) stored in the ESP32's flash memory.

### 2. `// --- CONFIGURATION ---`

This section defines the network credentials.

* `WIFI_SSID` & `WIFI_PASS`: Constants storing your WiFi network name and password.


### 3. **`// --- CERTIFICATES ---`**
This variable defines the **file path** inside the LittleFS partition.

* `ROOT_CA`: Points to the server's public certificate file path (e.g., `/root-ca.crt`). This is used to verify the server's identity during the secure handshake.


### 4. `// --- OBJECTS ---`

* `WiFiClientSecure net;`: Instantiates the secure client object. This object will hold the loaded certificates and manage the encryption layer (handshake, encryption, decryption) for the connection.

### 5. `// --- FUNCTIONS ---`

#### `connectWifi()`

* **Purpose:** Establishes a standard connection to the local network.
* **Logic:** It calls `WiFi.begin()` and enters a `while` loop, checking `WiFi.status()` every 500ms until it receives the `WL_CONNECTED` signal.

#### `SyncTime()`

* **Purpose:** Synchronizes the internal clock via NTP (Network Time Protocol).
* **Why it is Critical:** X.509 Certificates have "Valid From" and "Valid To" timestamps. If the ESP32 thinks the date is Jan 1, 1970 (default boot time), it will reject the server's certificate as "not yet valid."
* **Logic:** It connects to `pool.ntp.org` and waits until the system time is greater than a set epoch threshold (ensuring time was updated).

#### `readFile(const char* path)`

* **Purpose:** A helper function to read a file from storage into the device's RAM.
* **Key Steps:**
  1. **`LittleFS.open(path, "r")`**: Opens the file in read-only mode.
  2. **`malloc(size + 1)`**: Dynamically allocates memory based on the file size. The `+1` reserves space for a Null Terminator.
  3. **`buf[size] = '\0'`**: Manually adds a Null Terminator at the end of the data. This converts the raw file data into a **C-String**, which is the required format for the `WiFiClientSecure` library.

#### **`loadCertificates()`**
* **Purpose:** Handles the loading of the Root CA certificate.


* **Logic:** 
  1.  `LittleFS.begin(true)`: Mounts the file system. The `true` parameter formats the drive if it fails to mount (useful for first-time setup).
  2. **Read & Set:** It calls `readFile()` for the Root CA.
  3. **Validation:** Checks if the returned buffer is empty (`\0`).

  4. **Injection:** If valid, it passes the data to the `net` object using:
     * `net.setCACert(caCert)`: Loads server validation.

     * `net.setInsecure()`: Relaxes strict certificate validation. This is commonly used in PSK or development environments to ensure connection even if strict chain validation encounters minor issues, while still maintaining encryption.


### 6. `// --- SETUP ---`

`setup():` The standard Arduino initialization function that runs once at boot.

1. **`Serial.begin(115200)`**: Starts the serial monitor for debugging output.
2. **`connectWifi()`**: Gets internet access.
3. **`SyncTime()`**: Ensures the clock is correct **before** attempting any secure handshake.
4. **`loadCertificates()`**: Reads the Root CA file and prepares the secure client.

At the end of `setup()`, the `net` object is fully configured and ready to be passed to an MQTT client (like `PubSubClient`) to establish a secure connection.

## 3.3 MQTT Connection Setup

This section covers how to establish a persistent connection to the Protonest MQTT Broker. It details the specific broker credentials required and provides connection logic for both X.509 and PSK authentication methods.

### Broker Configuration

Your firmware must use these exact parameters to successfully handshake with the Protonest cloud.

| Parameter | Value | Requirement |
| --- | --- | --- |
| **Broker Host** | `mqtt.protonest.co` | Do not use IP addresses; they may rotate. |
| **Port** | `8883` | Requires TLS 1.2+ (Secure MQTT). |
| **Client ID** | `DEVICE_NAME` | Must exactly match the Device Name registered in the dashboard (e.g., `Pump_A_Controller`). |
| **Keep Alive** | `60` seconds | Recommended interval to maintain the connection. |

---

### Method 1: X.509 Certificate Connection 


To upgrade the previous Authentication Setup code([X.509](#3_2_X509_Auth_example)) to support MQTT, we add the following components:


1. **Under** `// --- INCLUDES ---`, add this part.
We include `PubSubClient.h` to handle the MQTT protocol.

```cpp
#include <PubSubClient.h>
```

2. **Under** `// --- CONFIGURATION ---`, add this part.
We define the broker address, port, and device name. The `DEVICE_NAME` serves as the MQTT Client ID.

```cpp
const char* MQTT_SERVER = "mqtt.protonest.co"; // Protonest MQTT Broker Address. ⚠️ Do not change.
const int MQTT_PORT = 8883;                    // Protonest MQTT Port. ⚠️ Do not change.
const char* DEVICE_NAME = "YOUR_DEVICE_NAME";   // Device Name should match the USERNAME
```
3. **Under** `// --- CERTIFICATES ---`, add this part. These variables define the **file paths** inside the LittleFS partition.
```cpp
// --- CERTIFICATES ---
const char* ROOT_CA_PATH = "/root-ca.crt";          // Root CA Certificate path (from downloaded zip file)
const char* CLIENT_CERT_PATH = "/device-cert.pem";  // Client Certificate path (from downloaded zip file)
const char* CLIENT_KEY_PATH = "/device-key.pem";    // Root CA Certificate path (from downloaded zip file)
```

1. **Under** `// --- OBJECTS ---`, add this part.
We initialize the `PubSubClient`, passing the secure network client (`net`) to it.

```cpp
PubSubClient client(net);
```

4. **Under** `// --- FUNCTIONS ---`, add this function.
We create `connectX509()` to handle the connection logic. It checks if the client is disconnected and attempts to reconnect using `client.connect(DEVICE_NAME)`.

```cpp
void connectX509() {
  // Reconnect Logic
  if (!client.connected()) {
    Serial.print("Connecting to Protonest (X.509)... ");
    if (client.connect(DEVICE_NAME)) {
      Serial.println("[X.509 SUCCESS]");
    } else {
      Serial.print("[FAILED] RC=");
      Serial.print(client.state());
      Serial.println(" -> Retrying in 5s...");
      delay(5000);
      return;
    }
  } else {
    client.loop();// Maintain connection
  }
}
```

5. **Under** `setup()`, add this part.
We initialize the MQTT server settings.

```cpp
client.setServer(MQTT_SERVER, MQTT_PORT); // MQTT Init
```

6. **Inside the** `loop()`, add this part.
We call the connection function to ensure the device stays connected.

```cpp
  connectX509(); // Reconnect if the connection is lost
```
<a id="3_3_X509_mqtt_conn_example"></a>
#### Full X.509 Code >> [VIEW](./X509_MQTT_CONN_CPP.md)

---

### Method 2: PSK (Pre-Shared Key) Connection

To upgrade the previous Authentication Setup code([PSK](#3_2_PSK_Auth_example)) to support MQTT, we add the following components:

1. **Under** `// --- INCLUDES ---`, add this part.
We include `PubSubClient.h` for MQTT functionality.

```cpp
#include <PubSubClient.h>
```

2. **Under** `// --- CONFIGURATION ---`, add this part.
We add the `USERNAME` and `PASSWORD` credentials. Crucially, we set `DEVICE_NAME = USERNAME` because Protonest requires the Client ID to match the authenticated identity.

```cpp
const char* USERNAME = "YOUR_USERNAME";  // Username from credentials.txt file
const char* PASSWORD = "YOUR_PASSWORD";  // Password from credentials.txt file

const char* MQTT_SERVER = "mqtt.protonest.co";  // Protonest MQTT Broker Address. ⚠️ Do not change.
const int MQTT_PORT = 8883;                     // Protonest MQTT Port. ⚠️ Do not change.
const char* DEVICE_NAME = USERNAME;             // Device Name should match the USERNAME
```

3. **Under** `// --- OBJECTS ---`, add this part.
We initialize `PubSubClient` linked to the secure network client.

```cpp
PubSubClient client(net);
```

4. **Under** `// --- FUNCTIONS ---`, add this function.
We create `connectPSK()`. The key difference here is we pass the `USERNAME` and `PASSWORD` to the `client.connect()` function.



```cpp
void connectPSK() {
  // Reconnect Logic
  if (!client.connected()) {
    Serial.print("Connecting to Protonest (PSK)... ");

    if (client.connect(DEVICE_NAME, USERNAME, PASSWORD)) {
      Serial.println("[PSK SUCCESS]");
    } else {
      Serial.print("[FAILED] RC=");
      Serial.print(client.state());
      Serial.println(" -> Retrying in 5s...");
      delay(5000);
      return;
    }
  }else{
    client.loop();
  }
}
```

5. **Inside** `setup()`, add this part.
We initialize the MQTT server settings.

```cpp
client.setServer(MQTT_SERVER, MQTT_PORT); // MQTT Init
```

6. **Inside** `loop()`, add this part.
We call the PSK connection function.

```cpp
connectPSK();  // Reconnect if the connection is lost
```
<a id="3_3_PSK_mqtt_conn_example"></a>

#### Full PSK Code >> [VIEW](./PSK_MQTT_CONN_CPP.md)


## 3.4 Publishing Data

Once connected, your device sends sensor data to the cloud using **MQTT Publishing**. To ensure Protonest can visualize your data correctly, you must follow specific rules for the **Topic Structure** and **Payload Format**.

### 1. Topic Structure

Protonest uses a hierarchical topic structure to organize data streams.

**Format:** `protonest/<DeviceID>/stream/<suffix>`

* `protonest/` : The root namespace for the platform.
* `<DeviceID>` : Your unique Device Name (e.g., `Pump_A`).
* `stream/` : Indicates this message contains telemetry data.
* `<suffix>` : Identifies the specific data type (e.g., `temp`, `humidity`, `voltage`).

**Examples:**

* Temperature Sensor: `protonest/Pump_A/stream/temp`
* Status Message: `protonest/Pump_A/stream/status`

### 2. JSON Payload Format

Data must be sent as a **JSON string**. This allows you to include metadata (like units) alongside the raw value.

**Standard Format:**

```json
{
  "value": 24.5,
  "unit": "C"
}
```

**Why JSON?**
Using JSON ensures the dashboard knows how to interpret the data (e.g., displaying "24.5°C" instead of just a raw number "24.5").

---

### Example Code Implementation

To enable MQTT publishing, add the following segments to above MQTT connection code ([X.509](#3_3_X509_mqtt_conn_example) or [PSK](#3_3_PSK_mqtt_conn_example)).

#### Step 1: Add Configuration Variables

**Under** `// --- CONFIGURATION ---`
Add a timer variable to control how often data is sent (e.g., every 5 seconds).

```cpp
const int MQTT_PUBLISH_INTERVAL = 5000; // Publish interval in milliseconds
unsigned long LAST_PUB = 0;

```

#### Step 2: Add the Publish Function

**Under** `// --- FUNCTIONS ---`
Add this helper function. It automatically constructs the correct topic and publishes the payload.

```cpp
void publishData(String deviceName, String data_type, String payload) {
  // Publish Data to MQTT Broker
  if (!client.connected()) return;  // Safety check

  // Construct the Topic: protonest/<DeviceID>/stream/<data_type>
  String topic = "protonest/" + deviceName + "/stream/" + data_type;

  // Publish Data
  Serial.print("Publishing to [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(payload);

  client.publish(topic.c_str(), payload.c_str());
}
```

#### Step 3: Add Loop Logic

**Inside** `void loop()`
Add this non-blocking timer to send data periodically without stopping the rest of your code.

```cpp
// --- PUBLISH DATA ---
if (millis() - LAST_PUB > MQTT_PUBLISH_INTERVAL) {
  LAST_PUB = millis();

  // Example 1: Publishing Temperature
  // Topic: protonest/DEVICE_NAME/stream/temp
  String tempPayload = "{\"value\": 24.5, \"unit\": \"C\"}";
  publishData(DEVICE_NAME, "temp", tempPayload);

  // Example 2: Publishing Device Status
  // Topic: protonest/DEVICE_NAME/stream/status
  String statusPayload = "{\"status\": \"active\", \"uptime\": 1200}";
  publishData(DEVICE_NAME, "status", statusPayload);
}
```

---

### Full Code: X.509 Version

This code combines the X.509 MQTT connection setup with the new Publishing logic. [VIEW](./X509_PUBLISH_CPP.md)


### Full Code: PSK Version

This code combines the PSK MQTT connection setup with the new publishing logic. [VIEW](./PSK_PUBLISH_CPP.md)


## 3.5 Subscribing to Commands

To control your device remotely (e.g., turning on an LED or a pump), the device must **subscribe** to a specific topic. When the Protonest Cloud publishes a command to this topic, the device receives the message and executes the corresponding action.

### 1. Topic Structure

The device listens for commands on a dedicated "updates" topic.

**Format:** `protonest/<DeviceID>/state/updates`

* `protonest/` : Root namespace.
* `<DeviceID>` : Your unique Device Name.
* `state/updates` : Indicates this channel is for receiving state change requests.

**Example:** `protonest/Pump_A/state/updates`

### 2. Payload Structure

Commands are sent as **JSON strings** containing a key (the component) and a value (the action).

**Example Payload:**

```json
{"led": "ON"}

```

or

```json
{"pump": "OFF"}

```

---

### Example Code Implementation

To enable command handling (MQTT subscribe), add the following segments to the MQTT connection code ([X.509](#3_3_X509_mqtt_conn_example) or [PSK](#3_3_PSK_mqtt_conn_example)).

#### Step 1: Pin Configuration

**Under** `// --- CONFIGURATION ---`
Define GPIO pins that will be controlled by the command.

```cpp
const int LED_PIN = 2;  // LED pin for MQTT Action
const int PUMP_PIN = 3; // Pump pin for MQTT Action
```

#### Step 2: Subscribe upon Connection

**Inside** `connectX509()` (or `connectPSK()`)
Immediately after a successful connection, tell the broker to send messages for the update topic.

```cpp
// --- SUBSCRIBE HERE ---
// Construct the topic dynamically: protonest/<DeviceID>/state/updates
String deviceTopic = "protonest/" + String(DEVICE_NAME) + "/state/updates";
client.subscribe(deviceTopic.c_str());
Serial.println("Subscribed to: " + deviceTopic);
```

#### Step 3: The Dispatcher (Callback Function)

**Under** `// --- FUNCTIONS ---`
This function runs automatically whenever a message arrives. It acts as a **Dispatcher**: it reads the message, identifies which component is being targeted (LED or Pump), and calls the correct action function.

```cpp
void callback(char* topic, byte* payload, unsigned int length) {
  // The MQTT Callback Function
  Serial.print("\nMessage received on [");
  Serial.print(topic);
  Serial.print("]: ");

  // 1. Convert Payload to String
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  // 2. Dispatcher Logic (Route based on keywords)

  // If the message is about the LED...
  if (message.indexOf("led") > 0) {
    handleLedControl(message);
  }

  // If the message is about the Pump...
  else if (message.indexOf("pump") > 0) {
    handlePumpControl(message);
  }
  // If no known command found
  else {
    Serial.println("   [Warning] Unknown command received.");
  }
}
```

#### Step 4: Action Functions

**Under** `// --- FUNCTIONS ---`
These functions handle the actual hardware logic. Separating them from the callback makes the code cleaner.

```cpp
// --- ACTION FUNCTION 1: LED Control ---
void handleLedControl(String command) {
  // Check specifically what the LED should do
  if (command.indexOf("ON") > 0) {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("   [ACTION] LED turned ON");
  } else if (command.indexOf("OFF") > 0) {
    digitalWrite(LED_PIN, LOW);
    Serial.println("   [ACTION] LED turned OFF");
  }
}

// --- ACTION FUNCTION 2: Pump/Motor Control ---
void handlePumpControl(String command) {
  if (command.indexOf("ON") > 0) {
    digitalWrite(PUMP_PIN, HIGH);
    Serial.println("   [ACTION] Pump Started");
  } else {
    digitalWrite(PUMP_PIN, LOW);
    Serial.println("   [ACTION] Pump Stopped");
  }
}
```

#### Step 5: Register the Callback and configure PINOUT.

**Inside** `setup()`
You must tell the MQTT client to use your `callback` function.

```cpp
client.setCallback(callback); // SUBSCRIPTION CALLBACK

pinMode(LED_PIN, OUTPUT); // Set LED_PIN as output
pinMode(PUMP_PIN, OUTPUT);// Set PUMP_PIN as output
```

---

### Full Code: X.509 Version

This code combines the X.509 MQTT connection setup with the new subscribing logic. [VIEW](./X509_SUBSCRIBE.md)

---

### Full Code: PSK Version

This code combines the PSK MQTT connection setup with the new subscribing logic. [VIEW](./PSK_SUBSCRIBE.md)


## 3.6 OTA Implementation

This section explains how to implement Over-The-Air (OTA) firmware updates. This feature allows you to remotely upgrade device software (Firmware) without physical access using a "Command-and-Download" pattern.

### Prerequisites: Generating Binary Files

Before implementing OTA, you must know how to generate the compiled `.bin` file that will be uploaded to the Protonest dashboard.

1. **Arduino IDE:** [How to Export Compiled Binary](https://www.google.com/search?q=https://docs.arduino.cc/software/ide-v1/tutorials/export-compiled-binary)

2. **PlatformIO:** [How to Build Firmware Image](https://docs.platformio.org/en/latest/core/userguide/cmd_run.html)

---

### The OTA Process

The OTA process involves three distinct stages: Receiving the command, Downloading the file, and Reporting the status.

**1. Receiving the Command (Subscribe)**
The device must subscribe to a specific topic to listen for update jobs.

* **Topic:** `protonest/<device_id>/ota/pending`
* **Payload Structure:**
```json
{
  "otaVersion": "2.0.0",
  "otaId": "33",
  "otaUrl": "https://protonest.co/device/ota/download/33/firmware.bin"
}
```


* `otaUrl`: The secure HTTPS link to the `.bin` firmware file.
* `otaId`: A unique Job ID used to track the status of this specific update.


**2. Downloading (HTTP Update)**
The device uses the `otaUrl` to initiate a secure file download via the `HTTPUpdate` library.

**3. Reporting Status (Publish)**
To keep the dashboard informed, the device must publish its status (Success or Failure) back to the cloud.

* **Topic:** `protonest/<device_id>/ota/status/update`
* **Payload Structure:**
```json
{
  "status": "completed", 
  "otaId": "33"
}
```

* `status`: Must be `"completed"` or `"failed"`.

---

###  Example Code Implementation

To enable OTA functionality, add the following segments to the MQTT connection code ([X.509](#3_3_X509_mqtt_conn_example) or [PSK](#3_3_PSK_mqtt_conn_example)).

#### 1. Include Libraries

**Under** `// --- INCLUDES ---`

We include `ArduinoJson.h` for JSON parsing and `HTTPUpdate.h` to handle the OTA update.

```cpp
#include <ArduinoJson.h>
#include <HTTPUpdate.h>
```

#### 2. Subscribe to OTA Topic

**Inside** `connectX509()` (or `connectPSK()`)
Immediately after a successful connection, tell the broker to send messages for the update topic.

```cpp
// --- SUBSCRIBE HERE ---
// OTA updates topic
String otaTopic = "protonest/" + String(DEVICE_NAME) + "/ota/pending";
client.subscribe(otaTopic.c_str());
Serial.println("Subscribed to: " + otaTopic);
```

#### 3. Critical: Keep-Alive Callbacks

**Under** `// --- FUNCTIONS ---`The `httpUpdate.update()` function is **blocking**. If the download takes longer than 60 seconds, the broker will disconnect the device. We use these callbacks to trigger `client.loop()` *during* the download.

```cpp
void ota_update_started() {
  Serial.println("CALLBACK:  HTTP update process started");
    if (client.connected()) {
    client.loop();
  }
}

void ota_update_finished() {
  Serial.println("CALLBACK:  HTTP update process finished");
    if (client.connected()) {
    client.loop();
  }
}

void ota_update_progress(int cur, int total) {
  Serial.printf("CALLBACK:  HTTP update process at %d of %d bytes...\n", cur, total);
  if (client.connected()) {
    client.loop();
  }
}

void ota_update_error(int err) {
  Serial.printf("CALLBACK:  HTTP update fatal error code %d\n", err);
    if (client.connected()) {
    client.loop();
  }
}
```

#### 4. Helper: Publish OTA Status

**Under** `// --- FUNCTIONS ---`
This function is responsible for sending the acknowledgment back to the Protonest Dashboard. It constructs a JSON payload with the status (`completed` or `failed`) and the specific `otaId` associated with the job.

```cpp
void publishOTAStatus(String status, String otaId) {
  // --- Publish OTA status
  if (!client.connected()) {
    Serial.println("MQTT not connected. Cannot publish OTA status.");
    return;
  }

  String topic = "protonest/" + String(DEVICE_NAME) + "/ota/status/update";

  // Create JSON Payload
  String payload = "{\"status\": \"" + status + "\", \"otaId\": \"" + otaId + "\"}";

  Serial.print("OTA Status: ");
  Serial.println(payload);
  client.publish(topic.c_str(), payload.c_str());
}
```

#### 5. The OTA Handler Function

**Under** `// --- FUNCTIONS ---`
This function parses the incoming command, configures the secure client, and executes the update. It uses the helper function defined above to report the result.

```cpp
void handleOTAUpdate(String payload) {
  // OTA Update function
  Serial.println("OTA Update Initiated...");

  // 1. Parse the JSON Payload
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  const char* otaUrl = doc["otaUrl"];
  const char* otaId = doc["otaId"];
  const char* version = doc["otaVersion"];

  if (!otaUrl || !otaId) {
    Serial.println("Error: Missing OTA URL or ID");
    return;
  }

  Serial.printf("Downloading Firmware v%s from: %s\n", version, otaUrl);


  // 1. Create a NEW Secure Client for the file download
  // We do NOT use the main MQTT client because it is busy holding the connection.
  WiFiClientSecure otaClient;

  httpUpdate.onStart(ota_update_started);
  httpUpdate.onEnd(ota_update_finished);
  httpUpdate.onProgress(ota_update_progress);
  httpUpdate.onError(ota_update_error);

  // 2. Allow Insecure (Essential for redirects or self-signed certs)
  otaClient.setInsecure();

  // 3. Configure HTTP Update to follow redirects (Required for many file servers)
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  
  // 4. Disable automatic reboot
  httpUpdate.rebootOnUpdate(false);

  // 5. Start Update (Blocking Call)
  // This will freeze the device until the download finishes or fails
  t_httpUpdate_return ret = httpUpdate.update(otaClient, otaUrl);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      publishOTAStatus("failed", otaId);
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES");
      publishOTAStatus("failed", otaId);
      break;

    case HTTP_UPDATE_OK:
      Serial.println("HTTP_UPDATE_OK");
      publishOTAStatus("completed", otaId);
      // Give time for the MQTT message to transmit
      unsigned long t = millis();
      while (millis() - t < 2000) client.loop();
      Serial.println("DEVICE RESTARTING.........");
      ESP.restart();
      break;
  }
}
```

---

### Full Code: X.509 Version (With OTA)

This code combines the X.509 MQTT connection setup with the new OTA update logic. [VIEW](./X509_OTA.md)

---

### Full Code: PSK Version (With OTA)

This code combines the PSK MQTT connection setup with the new OTA update logic. [VIEW](./PSK_OTA.md)

## 3.7 Extra Security: Secure Boot & Flash Encryption

By default, the ESP32's external flash memory is unencrypted. If a malicious actor gains physical access to your device, they can easily read your firmware, extract credentials (like your Private Keys or PSK passwords), or clone your IP.

To prevent this, Espressif provides two hardware-level security features: **Secure Boot** and **Flash Encryption**.

### 1. Secure Boot V2

Secure Boot ensures that the device only runs authorized firmware. It creates a "Chain of Trust" from the hardware to the application.

* **How it works:** You generate a private signing key on your computer. Every time you compile firmware, you "sign" it with this key. The ESP32 holds the corresponding public key in its eFuse (One-Time Programmable memory). On boot, the chip verifies the signature. If the firmware is unsigned or signed with the wrong key, the device refuses to boot.
* **Protection against:** Malicious firmware injection and unauthorized OTA updates.

### 2. Flash Encryption

Flash Encryption encrypts the contents of the ESP32's SPI flash memory using AES-256.

* **How it works:** When enabled, the ESP32 generates a random encryption key and burns it into a hidden eFuse block (which cannot be read by software). All data written to the flash is automatically encrypted, and data read from the flash is decrypted on-the-fly by the hardware.
* **Protection against:** Physical readout of firmware, cloning of devices, and extraction of certificates/credentials stored in LittleFS/SPIFFS.

---

### ⚠️ Critical Warning (Read Before Enabling)

Enabling Secure Boot and Flash Encryption involves burning **eFuses** inside the ESP32 chip.

* **This process is irreversible.** Once enabled, you cannot disable it.
* **Risk of Bricking:** If you lose your signing key or burn the wrong configuration, the ESP32 will become permanently unusable ("bricked").
* **Development vs. Production:** Do **not** enable these on your primary development board while you are still debugging. These features are intended for the final production stage.

### Implementation Guides

Due to the complexity and risk involved, please refer to the official Espressif documentation for the exact steps tailored to your specific ESP32 variant (S2, C3, S3, etc.).

* **Secure Boot V2 Guide:** [Espressif Docs - Secure Boot](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/secure-boot-v2.html)
* **Flash Encryption Guide:** [Espressif Docs - Flash Encryption](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/flash-encryption.html)
