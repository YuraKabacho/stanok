#ifndef CONFIG_H
#define CONFIG_H

// Pin Definitions
const int encoderPins[] = {32, 33, 25}; // A, B, BTN
const int motorPins[4][2] = {
  {13, 12}, // M1: IN1, IN2
  {27, 26}, // M2: IN1, IN2
  {14, 4},  // M3: IN1, IN2
  {2, 15}   // M4: IN1, IN2
};
const int limitPins[] = {5, 23, 35, 34};
const int servoPins[] = {18, 19};

// Parameters
const int max_mm = 20;
const int min_mm = 0;
const int ms_per_mm = 6800;
#define ENCODER_DEBOUNCE 40

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#endif