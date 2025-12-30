#### Publishing Data - Full Code for X.509 Certificate Connection

```cpp
// --- INCLUDES ---
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <PubSubClient.h>

// --- CONFIGURATION ---
const char* WIFI_SSID = "YOUR_WIFI_SSID";  // Your WiFi SSID
const char* WIFI_PASS = "YOUR_WIFI_PASS";           // Your WiFi Password

const char* MQTT_SERVER = "mqtt.protonest.co";  // Protonest MQTT Broker Address. ⚠️ Do not change.
const int MQTT_PORT = 8883;                     // Protonest MQTT Port. ⚠️ Do not change.
const char* DEVICE_NAME = "YOUR_DEVICE_NAME";     // Device Name should match the USERNAME

int const MQTT_PUBLISH_INTERVAL = 5000;  // Publish interval in milliseconds
int unsigned long LAST_PUB = 0;

// --- CERTIFICATES ---
const char* ROOT_CA_PATH = "/root-ca.crt";                  // Root CA Certificate path (from downloaded zip file)
const char* CLIENT_CERT_PATH = "/device-cert.pem";  // Client Certificate path (from downloaded zip file)
const char* CLIENT_KEY_PATH = "/device-key.pem";    // Root CA Certificate path (from downloaded zip file)

// --- OBJECTS ---
WiFiClientSecure net;
PubSubClient client(net);

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

// --- SETUP ---
void setup() {
  Serial.begin(115200); // Start Serial Debuging
  connectWifi();       // Connect to WiFi
  SyncTime();          // Sync Time (CRITICAL for host validation)
  loadCertificates();  // Load Certs

  client.setServer(MQTT_SERVER, MQTT_PORT); // MQTT Init

  // Your setup code here
}

// --- LOOP ---
void loop() {
  connectX509(); // Reconnect if the connection is lost
  
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
}
```