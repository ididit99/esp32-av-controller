#ifndef TERMINAL_HANDLER_H
#define TERMINAL_HANDLER_H

#include "AppConfig.h"
#include <WiFi.h>

extern WiFiClient termClient;
extern bool termConnected;
extern String termHost;
extern uint16_t termPort;

void termSendStatus();
void termDisconnect();
void termPumpTask(void *pvParameters);

#endif
