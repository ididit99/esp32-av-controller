#include "TerminalHandler.h"
#include "Utils.h"
#include <ArduinoJson.h>


WiFiClient termClient;
bool termConnected = false;
String termHost = "";
uint16_t termPort = 0;

void termSendStatus() {
  JsonDocument d;
  d["type"] = "status";
  d["connected"] = termClient.connected();
  d["host"] = termHost;
  d["port"] = termPort;
  String s;
  serializeJson(d, s);
  wsTerm.textAll(s);
}

void termDisconnect() {
  if (termClient.connected())
    termClient.stop();
  termConnected = false;
  termHost = "";
  termPort = 0;
  termSendStatus();
}

void termPumpTask(void *) {
  for (;;) {
    if (termClient.connected()) {
      while (termClient.available()) {
        uint8_t buf[256];
        int n = termClient.read(buf, sizeof(buf));
        if (n > 0) {
          JsonDocument d;
          d["type"] = "rx";
          d["hex"] = bytesToHex(buf, (size_t)n);
          d["ascii"] = bytesToAscii(buf, (size_t)n);
          String s;
          serializeJson(d, s);
          wsTerm.textAll(s);
        }
      }
    } else {
      if (termConnected)
        termDisconnect();
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}
