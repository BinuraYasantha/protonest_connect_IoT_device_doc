#### OTA Update - Full Code for X.509 Certificate Connection


```cpp
// --- INCLUDES ---
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPUpdate.h>

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
      Serial.println("[SUCCESS]");
      // --- SUBSCRIBE HERE ---
      // OTA updates topic
      String otaTopic = "protonest/" + String(DEVICE_NAME) + "/ota/pending";
      client.subscribe(otaTopic.c_str());
      Serial.println("Subscribed to: " + otaTopic);
    } else {
      Serial.print("[FAILED] RC=");
      Serial.print(client.state());
      Serial.println(" -> Retrying in 5s...");
      delay(5000);
      return;
    }
  } else {
    client.loop();
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  // MQTT message callback function
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
  if (message.indexOf("otaVersion") > 0) {
    handleOTAUpdate(message);
  }
  // If no known command found
  else {
    Serial.println("   [Warning] Unknown command received.");
  }
}

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

// --- SETUP ---
void setup() {
  Serial.begin(115200); // Start Serial Debuging
  connectWifi();       // Connect to WiFi
  SyncTime();          // Sync Time (CRITICAL for host validation)
  loadCertificates();  // Load Certs

  client.setServer(MQTT_SERVER, MQTT_PORT); // MQTT Init

  client.setCallback(callback); // SUBSCRIPTION CALLBACK
  
  // Your setup code here
}

// --- LOOP --
void loop() {
  connectX509();  // Reconnect if the connection is lost
  
  // Your loop code here
}
```
