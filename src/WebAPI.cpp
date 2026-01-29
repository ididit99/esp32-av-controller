#include "WebAPI.h"
#include <ArduinoJson.h>
#include <ESP32Ping.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFiUdp.h>

#include "AVDiscovery.h"
#include "CaptureProxy.h"
#include "ConfigManager.h"
#include "TerminalHandler.h"
#include "Utils.h"
#include "WiFiHelper.h"

extern bool learnEnabled;
extern uint16_t learnPort;

void setupRoutes() {
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.on("/api/health", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["fw"] = FW_VERSION;
    doc["uptime_s"] = (millis() - bootMs) / 1000;
    doc["heap_free"] = ESP.getFreeHeap();

    doc["wifi"]["mode"] = wifiCfg.mode;
    doc["wifi"]["staConnected"] = (WiFi.status() == WL_CONNECTED);
    doc["wifi"]["staIp"] = WiFi.localIP().toString();
    doc["wifi"]["staSsid"] = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "";
    doc["wifi"]["apIp"] = WiFi.softAPIP().toString();
    doc["wifi"]["apSsid"] = wifiCfg.apSsid;
    doc["wifi"]["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;

    doc["learn"]["enabled"] = learnEnabled;
    doc["learn"]["port"] = learnPort;

    doc["term"]["connected"] = termClient.connected();
    doc["term"]["host"] = termHost;
    doc["term"]["port"] = termPort;

    doc["proxy"]["running"] = proxyRunning;
    doc["proxy"]["listenPort"] = proxyListenPort;
    doc["proxy"]["targetHost"] = proxyTargetHost;
    doc["proxy"]["targetPort"] = proxyTargetPort;
    doc["proxy"]["captureToLearn"] = proxyCaptureToLearn;

    doc["disc"]["running"] = discRunning;
    doc["disc"]["progress"] = discProgress;

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html",
                  "<form method='POST' action='/update' "
                  "enctype='multipart/form-data'><input type='file' "
                  "name='update'><input type='submit' value='Update'></form>");
  });

  server.on(
      "/update", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(
            200, "application/json",
            shouldReboot ? "{\"ok\":true}" : "{\"error\":\"OTA Failed\"}");
        response->addHeader("Connection", "close");
        request->send(response);
      },
      [](AsyncWebServerRequest *request, String filename, size_t index,
         uint8_t *data, size_t len, bool final) {
        if (!index) {
          int cmd = U_FLASH;
          if (request->hasParam("type", true)) {
            String type = request->getParam("type", true)->value();
            if (type == "fs")
              cmd = U_SPIFFS;
          }
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
            Update.printError(Serial);
          }
        }
        if (!Update.hasError()) {
          if (Update.write(data, len) != len) {
            Update.printError(Serial);
          }
        }
        if (final) {
          if (Update.end(true)) {
            Serial.printf("Update Success: %uB\n", index + len);
          } else {
            Update.printError(Serial);
          }
        }
      });

  server.on("/api/rollback", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (Update.canRollBack()) {
      if (Update.rollBack()) {
        req->send(200, "application/json",
                  "{\"ok\":true, \"note\":\"Rolled back. Rebooting...\"}");
        shouldReboot = true;
      } else {
        req->send(500, "application/json", "{\"error\":\"Rollback failed\"}");
      }
    } else {
      req->send(
          400, "application/json",
          "{\"error\":\"Rollback not supported or no previous version\"}");
    }
  });

  wsLog.onEvent([](AsyncWebSocket *, AsyncWebSocketClient *c, AwsEventType t,
                   void *, uint8_t *, size_t) {
    if (t == WS_EVT_CONNECT)
      c->text("log connected");
  });
  server.addHandler(&wsLog);
  server.addHandler(&wsTerm);
  server.addHandler(&wsProxy);
  server.addHandler(&wsDisc);

  wsTerm.onEvent([](AsyncWebSocket *, AsyncWebSocketClient *c, AwsEventType t,
                    void *, uint8_t *data, size_t len) {
    if (t != WS_EVT_DATA)
      return;

    JsonDocument doc;
    if (deserializeJson(doc, data, len))
      return;

    String action = doc["action"] | "";
    if (action == "connect") {
      String host = doc["host"] | "";
      uint16_t port = doc["port"] | 0;
      termDisconnect();
      IPAddress ip;
      if (!ip.fromString(host)) {
        IPAddress resolved;
        if (WiFi.hostByName(host.c_str(), resolved) != 1) {
          c->text(R"({"type":"error","msg":"DNS failed"})");
          return;
        }
        ip = resolved;
      }
      termClient.setTimeout(1500);
      if (!termClient.connect(ip, port)) {
        c->text(R"({"type":"error","msg":"Connect failed"})");
        return;
      }
      termConnected = true;
      termHost = ip.toString();
      termPort = port;
      termSendStatus();
      logAll("Terminal connected to " + termHost + ":" + String(termPort));
      return;
    }

    if (action == "disconnect") {
      termDisconnect();
      logAll("Terminal disconnected");
      return;
    }

    if (action == "send") {
      if (!termClient.connected()) {
        c->text(R"({"type":"error","msg":"Not connected"})");
        return;
      }
      String mode = doc["mode"] | "ascii";
      String payload = doc["data"] | "";
      String suffix = doc["suffix"] | "";
      if (mode == "hex") {
        std::vector<uint8_t> bytes;
        if (!parseHexBytes(payload, bytes)) {
          c->text(R"({"type":"error","msg":"Bad hex"})");
          return;
        }
        termClient.write(bytes.data(), bytes.size());
      } else {
        String out = payload;
        if (suffix == "\\r")
          out += "\r";
        else if (suffix == "\\n")
          out += "\n";
        else if (suffix == "\\r\\n")
          out += "\r\n";
        else if (suffix.length())
          out += suffix;
        termClient.print(out);
      }
      c->text(R"({"type":"tx","ok":true})");
      return;
    }
  });

  server.on("/api/ping", HTTP_GET, [](AsyncWebServerRequest *req) {
    String host =
        req->hasParam("host") ? req->getParam("host")->value() : "8.8.8.8";
    bool ret = Ping.ping(host.c_str(), 1);
    JsonDocument doc;
    doc["host"] = host;
    doc["ok"] = ret;
    doc["avg_time_ms"] = Ping.averageTime();
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/ssdp/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
    WiFiUDP udp;
    udp.begin(0);
    const char *msg =
        "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: "
        "\"ssdp:discover\"\r\nMX: 1\r\nST: ssdp:all\r\n\r\n";
    udp.beginPacket("239.255.255.250", 1900);
    udp.write((const uint8_t *)msg, strlen(msg));
    udp.endPacket();

    uint32_t start = millis();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    while (millis() - start < 2000) {
      int len = udp.parsePacket();
      if (len > 0) {
        String s;
        while (udp.available())
          s += (char)udp.read();
        JsonObject o = arr.add<JsonObject>();
        o["ip"] = udp.remoteIP().toString();
        int locIdx = s.indexOf("LOCATION:");
        if (locIdx < 0)
          locIdx = s.indexOf("Location:");
        if (locIdx >= 0) {
          int eq = locIdx + 9;
          int end = s.indexOf("\r", eq);
          o["loc"] = s.substring(eq, end);
          o["loc"].as<String>().trim();
        }
      }
      delay(10);
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
    bool fresh =
        req->hasParam("fresh") && req->getParam("fresh")->value() == "1";
    req->send(200, "application/json", doScan(fresh));
  });

  server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["mode"] = wifiCfg.mode;
    doc["staSsid"] = wifiCfg.staSsid;
    doc["apSsid"] = wifiCfg.apSsid;
    doc["apChan"] = wifiCfg.apChan;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on(
      "/api/wifi", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        wifiCfg.mode = doc["mode"] | wifiCfg.mode;
        String staSsid = doc["staSsid"] | "";
        String staPass = doc["staPass"] | "";
        if (staSsid.length())
          wifiCfg.staSsid = staSsid;
        if (staPass.length())
          wifiCfg.staPass = staPass;
        String apSsid = doc["apSsid"] | "";
        String apPass = doc["apPass"] | "";
        if (apSsid.length())
          wifiCfg.apSsid = apSsid;
        if (apPass.length())
          wifiCfg.apPass = apPass;
        if (doc["apChan"].is<uint8_t>())
          wifiCfg.apChan = doc["apChan"].as<uint8_t>();
        saveWifi();
        req->send(200, "application/json",
                  "{\"ok\":true,\"note\":\"reboot required\"}");
      });

  server.on("/api/wifi/forget", HTTP_POST, [](AsyncWebServerRequest *req) {
    wifiCfg.staSsid = "";
    wifiCfg.staPass = "";
    wifiCfg.mode = "ap";
    saveWifi();
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_AP);
    req->send(200, "application/json",
              "{\"ok\":true,\"note\":\"Forget OK. Rebooting...\"}");
    shouldReboot = true;
  });

  server.on("/api/scan/subnet", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (discRunning) {
      req->send(409, "application/json",
                "{\"error\":\"scan already running\"}");
      return;
    }
    startDisc();
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.on(
      "/api/discovery/start", HTTP_POST, [](AsyncWebServerRequest *req) {},
      nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        String subnet = doc["subnet"] | "";
        uint8_t from = doc["from"] | 1;
        uint8_t to = doc["to"] | 254;
        std::vector<uint16_t> ports;
        if (doc["ports"].is<JsonArray>()) {
          for (JsonVariant v : doc["ports"].as<JsonArray>()) {
            uint16_t p = v.as<uint16_t>();
            if (p > 0)
              ports.push_back(p);
          }
        }
        if (ports.empty())
          ports = {23, 80, 443, 5000, 1515, 6100};
        if (!subnet.length()) {
          IPAddress ip = WiFi.localIP();
          subnet = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]);
        }
        bool ok = true; // startDiscovery implementation needed
        // For now, call the one we have
        startDisc();
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on("/api/discovery/results", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["running"] = discRunning;
    doc["progress"] = discProgress;
    JsonArray arr = doc["results"].to<JsonArray>();
    for (auto &line : discFound) {
      JsonDocument row;
      if (!deserializeJson(row, line))
        arr.add(row.as<JsonObject>());
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/captures", HTTP_GET, [](AsyncWebServerRequest *req) {
    String filter =
        req->hasParam("filter") ? req->getParam("filter")->value() : "";
    bool pinnedOnly =
        req->hasParam("pinned") && req->getParam("pinned")->value() == "1";
    JsonDocument doc;
    JsonArray arr = doc["captures"].to<JsonArray>();
    for (int i = (int)caps.size() - 1; i >= 0; i--) {
      const auto &c = caps[i];
      if (pinnedOnly && !c.pinned)
        continue;
      if (filter.length() && c.srcIp.indexOf(filter) < 0)
        continue;
      JsonObject o = arr.add<JsonObject>();
      o["id"] = c.id;
      o["ts"] = c.ts;
      o["srcIp"] = c.srcIp;
      o["srcPort"] = c.srcPort;
      o["localPort"] = c.localPort;
      o["hex"] = c.hex;
      o["ascii"] = c.ascii;
      o["pinned"] = c.pinned;
      o["repeats"] = c.repeats;
      o["lastTs"] = c.lastTs;
      o["suffixHint"] = c.suffixHint;
      o["payloadType"] = c.payloadType;
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on(
      "/api/capture/pin", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        String id = doc["id"] | "";
        bool pin = doc["pin"] | true;
        for (auto &c : caps)
          if (c.id == id) {
            c.pinned = pin;
            break;
          }
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", cfgJson);
  });

  server.on(
      "/api/config", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument tmp;
        if (deserializeJson(tmp, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        cfgJson = String((const char *)data).substring(0, len);
        saveCfg();
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on("/api/devices", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    if (deserializeJson(doc, cfgJson)) {
      req->send(500, "application/json", "{\"error\":\"cfg parse\"}");
      return;
    }
    String out;
    serializeJson(doc["devices"], out);
    req->send(200, "application/json", out);
  });

  server.on("/api/devices/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    JsonArray arr = doc["status"].to<JsonArray>();
    for (auto &s : devStatuses) {
      JsonObject o = arr.add<JsonObject>();
      o["id"] = s.id;
      o["online"] = s.online;
      o["lastSeenMs"] = s.lastSeenMs;
      o["ip"] = s.lastIp;
      o["port"] = s.lastPort;
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on(
      "/api/devices/add", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument d;
        if (deserializeJson(d, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        String name = d["name"] | "Device";
        String ip = d["ip"] | "";
        uint16_t portHint = d["portHint"] | 0;
        String suffixHint = d["defaultSuffix"] | "";
        String notes = d["notes"] | "";
        String templateId = d["templateId"] | "";
        String payloadType = d["defaultPayloadType"] | "";
        String mac = d["mac"] | "";
        if (!ip.length()) {
          req->send(400, "application/json", "{\"error\":\"missing ip\"}");
          return;
        }
        if (!updateCfgWithDevice(name, ip, portHint, suffixHint, notes,
                                 templateId, payloadType, mac)) {
          req->send(500, "application/json",
                    "{\"error\":\"cfg update failed\"}");
          return;
        }
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on(
      "/api/devices/delete", HTTP_POST, [](AsyncWebServerRequest *req) {},
      nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        String id = doc["id"] | "";
        if (removeDevice(id))
          req->send(200, "application/json", "{\"ok\":true}");
        else
          req->send(404, "application/json", "{\"error\":\"not found\"}");
      });

  server.on(
      "/api/pjlink", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        String ip = doc["ip"] | "";
        String pass = doc["pass"] | "";
        String cmd = doc["cmd"] | "";
        if (!ip.length() || !cmd.length()) {
          req->send(400, "application/json",
                    "{\"error\":\"missing ip or cmd\"}");
          return;
        }
        String resp = pjlinkCmd(ip, pass, cmd);
        JsonDocument res;
        res["response"] = resp;
        String out;
        serializeJson(res, out);
        req->send(200, "application/json", out);
      });

  server.on(
      "/api/mdns/scan", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        String service = doc["service"] | "_http";
        String proto = doc["proto"] | "tcp";
        int n = MDNS.queryService(service.c_str(), proto.c_str());
        JsonDocument res;
        res["count"] = n;
        JsonArray arr = res["results"].to<JsonArray>();
        for (int i = 0; i < n; ++i) {
          JsonObject o = arr.add<JsonObject>();
          o["hostname"] = MDNS.hostname(i);
          o["ip"] = MDNS.IP(i).toString();
          o["port"] = MDNS.port(i);
        }
        String out;
        serializeJson(res, out);
        req->send(200, "application/json", out);
      });

  server.on(
      "/api/wol", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        String mac = doc["mac"] | "";
        sendWol(mac);
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on(
      "/api/proxy/start", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        proxyListenPort = doc["listenPort"] | proxyListenPort;
        proxyTargetHost = (const char *)(doc["targetHost"] | "");
        proxyTargetPort = doc["targetPort"] | 0;
        proxyCaptureToLearn = doc["captureToLearn"] | false;
        proxyStart();
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on("/api/proxy/stop", HTTP_POST, [](AsyncWebServerRequest *req) {
    proxyStop();
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
  });
  server.on(
      "/api/learner", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        if (doc.containsKey("enabled"))
          learnEnabled = doc["enabled"];
        if (doc.containsKey("port"))
          learnPort = doc["port"];

        // If enabling, we might want to ensure the server is restarted or
        // relevant logic applied, but for now just updating globals as
        // requested.
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on("/api/capture/get", HTTP_GET, [](AsyncWebServerRequest *req) {
    String id = req->hasParam("id") ? req->getParam("id")->value() : "";
    JsonDocument doc;
    bool found = false;
    for (const auto &c : caps) {
      if (c.id == id) {
        doc["id"] = c.id;
        doc["ts"] = c.ts;
        doc["srcIp"] = c.srcIp;
        doc["srcPort"] = c.srcPort;
        doc["localPort"] = c.localPort;
        doc["hex"] = c.hex;
        doc["ascii"] = c.ascii;
        doc["pinned"] = c.pinned;
        doc["repeats"] = c.repeats;
        doc["lastTs"] = c.lastTs;
        doc["suffixHint"] = c.suffixHint;
        doc["payloadType"] = c.payloadType;
        found = true;
        break;
      }
    }
    if (found) {
      String out;
      serializeJson(doc, out);
      req->send(200, "application/json", out);
    } else {
      req->send(404, "application/json", "{\"error\":\"not found\"}");
    }
  });

  server.on("/api/discovery/stop", HTTP_POST, [](AsyncWebServerRequest *req) {
    discRunning = false;
    // We might want to give it a moment or rely on the loop checking the flag
    req->send(200, "application/json", "{\"ok\":true}");
  });
}
