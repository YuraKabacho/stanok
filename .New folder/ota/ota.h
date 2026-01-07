#ifndef OTA_H
#define OTA_H

#include <Arduino.h>
#include <ArduinoOTA.h>
#include "../oled/oled.h"
#include "../motor/motor.h"

class OTAHandler {
public:
  OTAHandler();
  void begin();
  void handle();
  void setupOTA();
  
private:
};

extern OTAHandler otaHandler;

#endif