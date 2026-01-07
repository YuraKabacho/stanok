#ifndef ENCODER_H
#define ENCODER_H

#include <Arduino.h>
#include "../config.h"

class Encoder {
public:
  Encoder();
  void begin();
  
  int8_t getDelta();
  bool isButtonPressed();
  
  void update();
  
  void setLastEncoderUpdate(unsigned long time) { lastEncoderUpdate = time; }
  unsigned long getLastEncoderUpdate() const { return lastEncoderUpdate; }
  
private:
  static void IRAM_ATTR readEncoder();
  
  static volatile int8_t encoder_delta;
  static unsigned long lastDebounce;
  static bool btnPressed;
  
  unsigned long lastEncoderUpdate = 0;
};

extern Encoder encoder;

#endif