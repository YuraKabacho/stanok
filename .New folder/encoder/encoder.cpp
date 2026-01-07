#include "encoder.h"

Encoder encoder;

volatile int8_t Encoder::encoder_delta = 0;
unsigned long Encoder::lastDebounce = 0;
bool Encoder::btnPressed = false;

Encoder::Encoder() {}

void Encoder::begin() {
  for (int i = 0; i < 3; i++) {
    pinMode(encoderPins[i], INPUT_PULLUP);
  }
  attachInterrupt(digitalPinToInterrupt(encoderPins[0]), readEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(encoderPins[1]), readEncoder, CHANGE);
}

void IRAM_ATTR Encoder::readEncoder() {
  static uint8_t lastState = 0;
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();
  
  if (interruptTime - lastInterruptTime < 5) return;
  lastInterruptTime = interruptTime;
  
  uint8_t state = (digitalRead(encoderPins[0]) << 1) | digitalRead(encoderPins[1]);
  uint8_t transition = (lastState << 2) | state;

  if (transition == 0b1101 || transition == 0b0100 || transition == 0b0010 || transition == 0b1011) encoder_delta++;
  if (transition == 0b1110 || transition == 0b0111 || transition == 0b0001 || transition == 0b1000) encoder_delta--;

  lastState = state;
}

int8_t Encoder::getDelta() {
  int8_t delta = encoder_delta;
  encoder_delta = 0;
  return delta;
}

bool Encoder::isButtonPressed() {
  bool btnState = digitalRead(encoderPins[2]);
  if (btnState == LOW && !btnPressed && millis() - lastDebounce > 300) {
    btnPressed = true;
    lastDebounce = millis();
    return true;
  } else if (btnState == HIGH && btnPressed) {
    btnPressed = false;
  }
  return false;
}

void Encoder::update() {
  // Additional encoder processing if needed
}