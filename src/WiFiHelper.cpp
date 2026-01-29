#include "WiFiHelper.h"
#include <ArduinoJson.h>
#include <esp_wifi.h>

WifiCfg wifiCfg;
static String lastScanJson =
    R"({"networks":[],"count":0,"note":"No scan yet"})";
static uint32_t lastScanMs = 0;

void wifiNoSleep() {
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
}

void loadWifi() {
  wifiCfg.mode = prefs.getString("w_mode", wifiCfg.mode);
  wifiCfg.staSsid = prefs.getString("w_staSsid", wifiCfg.staSsid);
  wifiCfg.staPass = prefs.getString("w_staPass", wifiCfg.staPass);
  wifiCfg.apSsid = prefs.getString("w_apSsid", wifiCfg.apSsid);
  wifiCfg.apPass = prefs.getString("w_apPass", wifiCfg.apPass);
  wifiCfg.apChan = prefs.getUChar("w_apChan", wifiCfg.apChan);

  // Never allow empty AP SSID (prevents "blank AP name" bug)
  if (!wifiCfg.apSsid.length())
    wifiCfg.apSsid = "ESP32-AV-Tool";
  if (wifiCfg.apPass.length() && wifiCfg.apPass.length() < 8)
    wifiCfg.apPass = "changeme123";
}

void saveWifi() {
  prefs.putString("w_mode", wifiCfg.mode);
  prefs.putString("w_staSsid", wifiCfg.staSsid);
  prefs.putString("w_staPass", wifiCfg.staPass);
  prefs.putString("w_apSsid", wifiCfg.apSsid);
  prefs.putString("w_apPass", wifiCfg.apPass);
  prefs.putUChar("w_apChan", wifiCfg.apChan);
}

void startWiFi() {
  wifiNoSleep();

  if (wifiCfg.mode == "sta")
    WiFi.mode(WIFI_STA);
  else if (wifiCfg.mode == "ap") {
    WiFi.disconnect(true, true); // Erase & Disconnect
    WiFi.mode(WIFI_AP);
    WiFi.setAutoConnect(false); // Do not let SDK auto-connect
  } else
    WiFi.mode(WIFI_AP_STA);

  if (wifiCfg.mode != "sta") {
    bool ok = WiFi.softAP(wifiCfg.apSsid.c_str(), wifiCfg.apPass.c_str(),
                          wifiCfg.apChan);
    logAll(String("AP ") + (ok ? "started" : "failed") +
           " SSID=" + wifiCfg.apSsid + " IP=" + WiFi.softAPIP().toString());
  }

  if (wifiCfg.mode != "ap" && wifiCfg.staSsid.length()) {
    WiFi.begin(wifiCfg.staSsid.c_str(), wifiCfg.staPass.c_str());
    logAll("STA connecting: " + wifiCfg.staSsid);
  } else {
    // CRITICAL: Ensure no ghost connection from SDK NVS
    WiFi.disconnect(true);
  }
}

String doScan(bool forceFresh) {
  // 1. Cache Check
  const uint32_t cacheMs = 15000; // 15s cache
  if (!forceFresh && lastScanMs != 0 && (millis() - lastScanMs) < cacheMs) {
    if (lastScanJson.indexOf("\"count\":0") < 0)
      return lastScanJson;
  }

  // 2. Prep Radio
  wifi_mode_t oldMode = WiFi.getMode();
  bool switched = false;

  // If we are strictly AP, we need AP_STA to scan properly on many cores
  if (oldMode == WIFI_AP) {
    WiFi.mode(WIFI_AP_STA);
    switched = true;
    delay(200); // Increased delay for stability
  }

  // Ensure any previous scan is cleared
  WiFi.scanDelete();
  delay(100);

  Serial.println("Starting WiFi Scan...");

  // 3. Scan
  // scanNetworks(async, show_hidden, passive, time_per_ch)
  // Async=false (blocking)
  int16_t n = WiFi.scanNetworks(false, true, false, 300);
  Serial.printf("Scan found %d networks\n", n);

  // 4. Handle Result
  JsonDocument doc;
  doc["count"] = (n >= 0) ? n : 0;

  String note = "Scan Done.";
  if (n == WIFI_SCAN_FAILED)
    note = "Scan Failed (-1). Try again.";
  else if (n == WIFI_SCAN_RUNNING)
    note = "Scan Running (-2).";
  else if (n == 0)
    note = "No networks found.";

  doc["note"] = note;
  doc["debug_code"] = n;

  JsonArray arr = doc["networks"].to<JsonArray>();
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      JsonObject o = arr.add<JsonObject>();
      o["ssid"] = WiFi.SSID(i);
      o["rssi"] = WiFi.RSSI(i);
      o["chan"] = WiFi.channel(i);
      o["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    }
  }

  // 5. Cleanup
  WiFi.scanDelete();

  if (switched) {
    // Return to AP-only mode if we were there
    WiFi.mode(WIFI_AP);
  }

  String out;
  serializeJson(doc, out);
  lastScanJson = out;
  lastScanMs = millis();
  return lastScanJson;
}

void bootScanTask(void *) {
  vTaskDelay(1200 / portTICK_PERIOD_MS);
  doScan(true);
  vTaskDelete(nullptr);
}
