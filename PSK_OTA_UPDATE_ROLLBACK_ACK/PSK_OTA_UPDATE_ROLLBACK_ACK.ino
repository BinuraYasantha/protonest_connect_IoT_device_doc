/*
  Main Firmware with Robust Runtime Monitoring & Reporting

  This sketch performs the following responsibilities:
  - Connects to WiFi and an MQTT broker using secure TLS
  - Receives an OTA update instruction via MQTT
  - Performs an OTA update using `httpUpdate`
  - Uses `OTADiagnostics` to detect crash loops or instability during a probation period
  - If an OTA introduces instability, OTADiagnostics will rollback and this firmware will
    report the rollback reason and OTA ID back to the cloud on the next successful boot
*/

// --- INCLUDES ---
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPUpdate.h>
#include "OTADiagnostics.h"

// --- CONFIGURATION ---
const char* WIFI_SSID = "YOUR_WIFI_SSID";  // Your WiFi SSID
const char* WIFI_PASS = "YOUR_WIFI_PASS";           // Your WiFi Password

const char* USERNAME = "USERNAME";  // Device223344
const char* PASSWORD = "PASSWORD";

const char* MQTT_SERVER = "mqtt.protonest.co";
const int MQTT_PORT = 8883;
const char* DEVICE_NAME = USERNAME;

const char* ROOT_CA_PATH = "/root-ca.crt";

// --- OBJECTS ---
WiFiClientSecure net;
PubSubClient client(net);
OTADiagnostics diag;

unsigned long lastReconnectAttempt = 0;
bool reportSent = false;  // To ensure we only report once per boot

// --- HELPER FUNCTIONS (WiFi/Files) ---

// --- ROBUST WIFI CONNECT ---
bool connectWifi() {
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  // Wait up to 15 seconds for WiFi
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 15000) {
      Serial.println("\n[Error] WiFi Connection Timeout!");
      return false; // Return FAIL
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected.");
  return true; // Return SUCCESS
}

// --- ROBUST TIME SYNC ---
bool SyncTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Syncing Time");

  unsigned long start = millis();
  // Wait up to 20 seconds for NTP
  while (time(nullptr) < 1000000000l) {
    if (millis() - start > 20000) {
      Serial.println("\n[Error] NTP Time Sync Timeout!");
      return false; // Return FAIL
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nTime Synced.");
  return true; // Return SUCCESS
}

// Read a file from LittleFS into a malloc buffer (caller must free)
char* readFile(const char* path) {
  File file = LittleFS.open(path, "r");
  if (!file) return "";
  size_t size = file.size();
  char* buf = (char*)malloc(size + 1);
  if (!buf) return nullptr;
  file.readBytes(buf, size);
  buf[size] = '\0';
  file.close();
  return buf;
}

// Load the root CA certificate from LittleFS (if present) into the secure client
void loadCertificates() {
  if (!LittleFS.begin(true)) return;
  char* caCert = readFile(ROOT_CA_PATH);
  if (caCert && caCert[0] != '\0') net.setCACert(caCert);
}

// --- REPORTING ---
// If the previous boot detected a rollback/failure, this function publishes a concise
// reason + the OTA ID that caused the rollback to the cloud so operators can triage.
void reportRollback() {
  if (reportSent) return; 

  int reasonCode = diag.getLastFailureReason();
  if (reasonCode != REASON_NONE) {
    String otaId = diag.getFailedOTAId();
    String reasonStr = "unknown";

    switch(reasonCode) {
      case REASON_CRASH_LOOP:   reasonStr = "runtime_error_crash"; break;
      case REASON_WIFI_TIMEOUT: reasonStr = "wifi_timeout"; break;
      case REASON_MQTT_TIMEOUT: reasonStr = "mqtt_timeout"; break;
      case REASON_UNSTABLE:     reasonStr = "connection_unstable"; break;
      case REASON_NTP_FAILURE:  reasonStr = "ntp_time_sync_failed"; break; // <--- NEW
    }

    Serial.printf("[MAIN] ⚠️ Reporting Failure: %s\n", reasonStr.c_str());

    String topic = "protonest/" + String(DEVICE_NAME) + "/ota/status/update";
    String payload = "{\"status\": \"failed\", \"reason\": \"" + reasonStr + "\", \"otaId\": \"" + otaId + "\"}";
    
    if (client.publish(topic.c_str(), payload.c_str())) {
      diag.clearFailure(); 
      reportSent = true;
    }
  }
}

// --- MQTT CONNECT + MAINTAIN ---
// Connect to MQTT using device credentials and subscribe to the OTA topic.
// When connected we immediately check if there's a rollback to report.
void connectPSK() {
  if (client.connected()) {
    client.loop();
    return;
  }

  unsigned long now = millis();
  if (now - lastReconnectAttempt > 5000) {
    lastReconnectAttempt = now;

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Connecting MQTT... ");
      if (client.connect(DEVICE_NAME, USERNAME, PASSWORD)) {
        Serial.println("[SUCCESS]");

        // 1. Subscribe to OTA Topic
        String otaTopic = "protonest/" + String(DEVICE_NAME) + "/ota/pending";
        client.subscribe(otaTopic.c_str());
        Serial.println("Subscribe to the OTA topic");

        // 2. CHECK IF WE NEED TO REPORT A FAILURE
        reportRollback();

      } else {
        Serial.print("[FAILED] RC=");
        Serial.println(client.state());
      }
    } else {
      WiFi.reconnect();
    }
  }
}

// MQTT message handler. We expect OTA instructions to contain an `otaVersion` field.
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];
  Serial.printf("Msg on [%s]: %s\n", topic, message.c_str());

  if (message.indexOf("otaVersion") > 0) {
    handleOTAUpdate(message);
  }
}

// --- OTA HANDLING ---
// Publish a small status update for OTA progress/result
void publishOTAStatus(String status, String otaId) {
  if (!client.connected()) return;
  String topic = "protonest/" + String(DEVICE_NAME) + "/ota/status/update";
  String payload = "{\"status\": \"" + status + "\", \"otaId\": \"" + otaId + "\"}";
  client.publish(topic.c_str(), payload.c_str());
}

// Parse OTA request, save the OTA ID (so we can report if it causes a rollback), and perform update
void handleOTAUpdate(String payload) {
  Serial.println("OTA Update Initiated...");

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) return; // Invalid JSON -> ignore

  const char* otaUrl = doc["otaUrl"];
  const char* otaId = doc["otaId"];
  const char* version = doc["otaVersion"];

  // Both URL and ID are required for a valid OTA instruction
  if (!otaUrl || !otaId) return;

  // --- CRITICAL: SAVE OTA ID FOR REPORTING ---
  // If this update fails and we rollback, the previous firmware will report this ID
  diag.setPendingOTA(String(otaId));
  // -------------------------------------------

  // Perform the HTTP update. 
  WiFiClientSecure otaClient;
  otaClient.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false); // we want to control when to restart

  t_httpUpdate_return ret = httpUpdate.update(otaClient, otaUrl);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA Failed: %s\n", httpUpdate.getLastErrorString().c_str());
      publishOTAStatus("failed", otaId);
      // If update failed before reboot, we clear pending ID (no rollback will happen)
      diag.clearFailure();
      break;

    case HTTP_UPDATE_NO_UPDATES:
      publishOTAStatus("failed", otaId);
      diag.clearFailure();
      break;

    case HTTP_UPDATE_OK:
      // Update succeeded and device will reboot into the new image.
      Serial.println("OTA OK. Rebooting into Probation Mode...");
      publishOTAStatus("completed", otaId);
      delay(1000);
      ESP.restart();
      break;
  }
}

// --- SETUP & LOOP ---
void setup() {
  Serial.begin(115200);
  diag.begin();  // Check for crash loops immediately

  // 1. Try WiFi
  if (!connectWifi()) {
    // If WiFi fails, we can't publish yet. 
    // Save error -> Restart -> Report on next boot.
    diag.triggerRollback(REASON_WIFI_TIMEOUT); 
  }

  // 2. Try Time Sync
  if (!SyncTime()) {
    // If Time fails, SSL will fail. 
    // Save error -> Restart -> Report on next boot.
    diag.triggerRollback(REASON_NTP_FAILURE); 
  }

  loadCertificates();

  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
  pinMode(9,OUTPUT);
}

void loop() {
  // 1. MONITOR STABILITY
  // If connected, this counts up to 60s.
  // If connection drops OR device crashes (Runtime error) -> it handles it.
  diag.check(WiFi.status() == WL_CONNECTED, client.connected());

  // 2. MAINTAIN CONNECTION
  connectPSK();

  digitalWrite(9, HIGH);
  delay(50);
  digitalWrite(9, LOW);
  delay(50);
}