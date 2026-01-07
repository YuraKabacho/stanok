#ifndef WIFIMANAGER_H
#define WIFIMANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

class WiFiManagerHandler {
public:
  WiFiManagerHandler();
  bool begin();
  
  String getLocalIP() const;
  bool isConnected() const;
  
private:
  WiFiManager wm;
  
  void setupWiFiManager();
};

extern WiFiManagerHandler wifiHandler;

#endif