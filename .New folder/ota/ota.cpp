#include "ota.h"

OTAHandler otaHandler;

OTAHandler::OTAHandler() {}

void OTAHandler::begin() {
  setupOTA();
}

void OTAHandler::handle() {
  ArduinoOTA.handle();
}

void OTAHandler::setupOTA() {
  ArduinoOTA.setHostname("esp32-stanok");
  ArduinoOTA.setPassword("ota123");

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      Serial.println("Start updating " + type);
      
      // Stop all motors for safety
      motorCtrl.stopAllMotors();
      
      // Clear OLED and show updating message
      oled.display.clearDisplay();
      oled.display.setTextSize(1);
      oled.display.setTextColor(SSD1306_WHITE);
      oled.display.setCursor(0, 0);
      oled.display.println("OTA UPDATE");
      oled.display.println("Updating " + type);
      oled.display.display();
    })
    .onEnd([]() {
      Serial.println("\nEnd");
      oled.display.clearDisplay();
      oled.display.setTextSize(1);
      oled.display.setTextColor(SSD1306_WHITE);
      oled.display.setCursor(0, 0);
      oled.display.println("Update Complete!");
      oled.display.println("Rebooting...");
      oled.display.display();
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      
      // Show progress on OLED
      oled.display.clearDisplay();
      oled.display.setTextSize(1);
      oled.display.setTextColor(SSD1306_WHITE);
      oled.display.setCursor(0, 0);
      oled.display.println("OTA UPDATE");
      oled.display.printf("Progress: %u%%", (progress / (total / 100)));
      oled.display.display();
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
      
      // Show error on OLED
      oled.display.clearDisplay();
      oled.display.setTextSize(1);
      oled.display.setTextColor(SSD1306_WHITE);
      oled.display.setCursor(0, 0);
      oled.display.println("OTA ERROR!");
      oled.display.printf("Error: %u", error);
      oled.display.display();
    });

  ArduinoOTA.begin();
  Serial.println("OTA Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Use your OTA tool to upload");
}