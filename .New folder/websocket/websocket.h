#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "../motor/motor.h"

class WebSocketHandler {
public:
  WebSocketHandler();
  void begin();
  void sendState();
  
  AsyncWebSocket ws;
  
private:
  AsyncWebServer server;
  
  void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                 AwsEventType type, void *arg, uint8_t *data, size_t len);
  void handleWebServer();
};

extern WebSocketHandler wsHandler;

#endif