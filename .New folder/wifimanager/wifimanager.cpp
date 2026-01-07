#include "wifimanager.h"

WiFiManagerHandler wifiHandler;

WiFiManagerHandler::WiFiManagerHandler() {}

bool WiFiManagerHandler::begin() {
  setupWiFiManager();
  
  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    Serial.println("Formatting LittleFS...");
    LittleFS.format();
    if (!LittleFS.begin()) {
      Serial.println("LittleFS mount failed after formatting");
      return false;
    }
  }
  Serial.println("LittleFS mounted successfully");
  
  // Configure WiFiManager
  wm.setConfigPortalTimeout(180); // 3 minutes timeout
  wm.setHostname("esp32-stanok");
  
  // Connect to WiFi
  bool res = wm.autoConnect("ESP32-Config", "config123");
  
  if (!res) {
    Serial.println("Failed to connect or configure");
    return false;
  }
  
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Hostname: ");
  Serial.println(WiFi.getHostname());
  
  return true;
}

void WiFiManagerHandler::setupWiFiManager() {
  // Additional WiFiManager configuration if needed
}

String WiFiManagerHandler::getLocalIP() const {
  return WiFi.localIP().toString();
}

bool WiFiManagerHandler::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}