/*
   OTADiagnostics.h

   Purpose:
   - Detects runtime crash loops (multiple consecutive boots within a short period)
   - Enforces a probation period after boot during which the firmware must remain
     connected and stable for a configured duration (STABILITY_DURATION)
   - If the firmware fails to prove stability within TOTAL_PROBATION_LIMIT or
     repeatedly crashes, `triggerRollback()` is called to attempt a rollback to
     the previous image.
   - Stores the failure reason and the OTA ID that caused it in persistent
     Preferences so the next boot can report the failure to the cloud.
*/

#ifndef OTADIAGNOSTICS_H
#define OTADIAGNOSTICS_H

#include <Arduino.h>
#include <Preferences.h> 
#include <Update.h>      

// --- FAILURE REASONS ---
// These are saved to flash so cloud reporting can include a human-friendly reason
enum FailureReason {
  REASON_NONE = 0,
  REASON_CRASH_LOOP,
  REASON_WIFI_TIMEOUT,
  REASON_MQTT_TIMEOUT,
  REASON_UNSTABLE,
  REASON_NTP_FAILURE // <--- NEW: Add this
};

class OTADiagnostics {
  private:
    Preferences prefs;               // Persistent storage for counters and failure state
    unsigned long _bootTime;         // millis() at begin()
    unsigned long _stabilityStartTime = 0; // When continuous connection started
    bool _validated = false;         // Whether current firmware has proven stable
    
    // --- CONFIGURATION ---
    const unsigned long STABILITY_DURATION = 60000; // require 60s continuous connection
    const unsigned long TOTAL_PROBATION_LIMIT = 300000; // 5 minutes to prove stability
    const int MAX_CRASH_ATTEMPTS = 3; // If we crash more than this, assume runtime fault and rollback

  public:
    // Called once during setup(): increments boot counter and checks for crash loops
    void begin() {
      prefs.begin("ota_diag", false);
      _bootTime = millis();
      _validated = false;
      _stabilityStartTime = 0;

      // Boot-count based crash detection:
      int boots = prefs.getInt("boot_count", 0);
      boots++;
      prefs.putInt("boot_count", boots);
      Serial.printf("[OTA Diag] Boot Attempt: %d\n", boots);

      // If we've booted repeatedly without validating, treat as crash loop and rollback
      if (boots > MAX_CRASH_ATTEMPTS) {
        Serial.println("[OTA Diag] ⚠️ CRASH LOOP DETECTED (Runtime Error)!");
        triggerRollback(REASON_CRASH_LOOP);
      }
    }

    // Continuously called from loop() with current connection state
    // - If validation completes, this will reset counters
    // - If probation timers elapse or connections fail repeatedly, this triggers rollback
    void check(bool isWifiConnected, bool isMqttConnected) {
      if (_validated) return; // Already validated this boot

      unsigned long now = millis();

      // GLOBAL TIMEOUT: If we don't validate within TOTAL_PROBATION_LIMIT, decide failure reason
      if (now - _bootTime > TOTAL_PROBATION_LIMIT) {
         Serial.println("[OTA Diag] ❌ Total Probation Timeout! System Unstable.");
         if (!isWifiConnected) triggerRollback(REASON_WIFI_TIMEOUT);
         else if (!isMqttConnected) triggerRollback(REASON_MQTT_TIMEOUT);
         else triggerRollback(REASON_UNSTABLE);
         return;
      }

      // STABILITY MONITOR: require STABILITY_DURATION of continuous WiFi+MQTT
      if (isWifiConnected && isMqttConnected) {
        // Start timer when the connection first becomes good
        if (_stabilityStartTime == 0) {
          _stabilityStartTime = now;
          Serial.println("[OTA Diag] Connection Established. Starting 60s Stability Check...");
        }

        // If we have been continuously connected for the stability duration, mark validated
        if (now - _stabilityStartTime > STABILITY_DURATION) {
          validateFirmware();
        }
      } else {
        // Any interruption resets the stability timer: we need continuous stability
        if (_stabilityStartTime != 0) {
           Serial.println("[OTA Diag] Connection Lost during Stability Check! Resetting timer.");
           _stabilityStartTime = 0;
        }
      }
    }

    // Mark the firmware as validated (stable): clear counters & failure flags
    void validateFirmware() {
      if (_validated) return;
      _validated = true;
      Serial.println("[OTA Diag] ✅ Firmware Validated! Runtime checks passed.");
      
      // Reset boot counter and clear last-failure info
      prefs.putInt("boot_count", 0);
      prefs.putInt("last_fail", REASON_NONE);
      prefs.putString("fail_ota_id", ""); // Clear any pending OTA ID
      
      // Optional: If using the ESP-IDF bootloader features, mark the app valid
      // esp_ota_mark_app_valid_cancel_rollback();
    }

    // Trigger rollback and persist failure reason + OTA ID for later reporting
    void triggerRollback(FailureReason reason) {
      Serial.printf("[OTA Diag] 🔄 Rolling Back... Reason: %d\n", reason);
      
      // Save reason and clear boot counter for a clean next boot
      prefs.putInt("last_fail", (int)reason);
      prefs.putInt("boot_count", 0); 
      prefs.end();

      // If a rollback partition is available, perform rollback and reboot
      if (Update.canRollBack()) {
        Update.rollBack();
        ESP.restart();
      } else {
        // No backup image available: reboot and hope for the best
        Serial.println("[OTA Diag] ❌ Critical: No Backup Partition! Restarting...");
        delay(2000);
        ESP.restart();
      }
    }

    // --- REPORTING HELPERS ---
    // Return the last persisted failure reason (REASON_NONE if none)
    int getLastFailureReason() {
      return prefs.getInt("last_fail", REASON_NONE);
    }

    // Return the OTA ID that was saved before the update (empty if none)
    String getFailedOTAId() {
      return prefs.getString("fail_ota_id", "");
    }

    // Clear persisted failure reason and OTA ID after they have been reported
    void clearFailure() {
      prefs.putInt("last_fail", REASON_NONE);
      prefs.putString("fail_ota_id", "");
    }

    // Save OTA ID before update starts so we can attribute a rollback to it later
    void setPendingOTA(String otaId) {
       prefs.putString("fail_ota_id", otaId);
    }
};

#endif