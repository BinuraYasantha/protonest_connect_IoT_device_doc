#### Subscribe to commands - Full Code for X.509 Certificate Connection


```cpp
// --- INCLUDES ---
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <PubSubClient.h>

// --- CONFIGURATION ---
const char* WIFI_SSID = "YOUR_WIFI_SSID5";  // Your WiFi SSID
const char* WIFI_PASS = "YOUR_WIFI_PASS";   // Your WiFi Password

const char* MQTT_SERVER = "mqtt.protonest.co"; // Protonest MQTT Broker Address. ⚠️ Do not change.
const int MQTT_PORT = 8883;                    // Protonest MQTT Port. ⚠️ Do not change.
const char* DEVICE_NAME = "YOUR_DEVICE_NAME";   // Device Name should match the USERNAME

// --- CERTIFICATES ---
const char* ROOT_CA_PATH = "/root-ca.crt";          // Root CA Certificate path (from downloaded zip file)
const char* CLIENT_CERT_PATH = "/device-cert.pem";  // Client Certificate path (from downloaded zip file)
const char* CLIENT_KEY_PATH = "/device-key.pem";    // Root CA Certificate path (from downloaded zip file)

const int LED_PIN = 2;  // LED pin for MQTT Action
const int PUMP_PIN = 3; // Pump pin for MQTT Action

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
      Serial.println("[MQTT Connection SUCCESS]");
      // --- SUBSCRIBE HERE ---
      // Construct the topic dynamically: protonest/<DeviceID>/state/updates
      String deviceTopic = "protonest/" + String(DEVICE_NAME) + "/state/updates";
      client.subscribe(deviceTopic.c_str());
      Serial.println("Subscribed to: " + deviceTopic);
    } else {
      Serial.print("[FAILED] RC=");
      Serial.print(client.state());
      Serial.println(" -> Retrying in 5s...");
      delay(5000);
      return;
    }
  } else {
    client.loop(); // Maintain connection
  }
}

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

// --- SETUP ---
void setup() {
  Serial.begin(115200); // Start Serial Debuging
  connectWifi();       // Connect to WiFi
  SyncTime();          // Sync Time (CRITICAL for host validation)
  loadCertificates();  // Load Certs

  client.setServer(MQTT_SERVER, MQTT_PORT); // MQTT Init

  client.setCallback(callback); // SUBSCRIPTION CALLBACK

  pinMode(LED_PIN, OUTPUT); // Set LED_PIN as output
  pinMode(PUMP_PIN, OUTPUT);// Set PUMP_PIN as output
  
  // Your setup code here
}

// --- LOOP ---
void loop() {
  connectX509();  // Reconnect if the connection is lost
  // Your loop code here
}
```