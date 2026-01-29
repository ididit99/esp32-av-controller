#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

// Firmware version
static const char *FW_VERSION =
    "0.5.0"; // Major update: OTA Tab, Discovery Fixes

// Global objects (defined in main.cpp for now, or a central location)
extern AsyncWebServer server;
extern AsyncWebSocket wsLog;
extern AsyncWebSocket wsTerm;
extern AsyncWebSocket wsProxy;
extern AsyncWebSocket wsDisc;
extern Preferences prefs;

extern uint32_t bootMs;
extern bool shouldReboot;

// Shared utilities
void logAll(const String &s);
void wsTextAll(AsyncWebSocket &ws, const String &s);

#endif
