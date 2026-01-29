#include "AVDiscovery.h"
#include "AppConfig.h"
#include "CaptureProxy.h"
#include "ConfigManager.h"
#include "TerminalHandler.h"
#include "Utils.h"
#include "WebAPI.h"
#include "WiFiHelper.h"
#include <ESPmDNS.h>
#include <LittleFS.h>


AsyncWebServer server(80);
AsyncWebSocket wsLog("/ws");
AsyncWebSocket wsTerm("/term");
AsyncWebSocket wsProxy("/wsproxy");
AsyncWebSocket wsDisc("/wsdisc");

Preferences prefs;
uint32_t bootMs;
bool shouldReboot = false;

void logAll(const String &s) {
  Serial.println(s);
  wsTextAll(wsLog, s);
}

void wsTextAll(AsyncWebSocket &ws, const String &s) { ws.textAll(s); }

void setup() {
  Serial.begin(115200);
  delay(150);
  bootMs = millis();

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  prefs.begin("avtool", false);
  loadWifi();
  loadCfg();

  startWiFi();

  xTaskCreatePinnedToCore(bootScanTask, "bootScan", 4096, nullptr, 1, nullptr,
                          1);
  startLearn();

  if (!MDNS.begin("esp32-av-tool")) {
    Serial.println("Error setting up MDNS responder!");
  }

  setupRoutes();
  server.begin();

  xTaskCreatePinnedToCore(termPumpTask, "termPump", 6144, nullptr, 1, nullptr,
                          1);
  xTaskCreatePinnedToCore(deviceMonitorTask, "devMon", 6144, nullptr, 1,
                          nullptr, 1);

  logAll(String("Ready FW ") + FW_VERSION + " UI: /  OTA: /update");
}

void loop() {
  if (shouldReboot) {
    delay(500);
    ESP.restart();
  }
  wsLog.cleanupClients();
  wsTerm.cleanupClients();
  wsProxy.cleanupClients();
  wsDisc.cleanupClients();
}
