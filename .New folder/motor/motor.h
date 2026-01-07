#ifndef MOTOR_H
#define MOTOR_H

#include <Arduino.h>
#include <ESP32Servo.h>
#include "../config.h"

struct Motor {
  int manual_distance = 0;
  int real_position = 0;
  int target = 0;
  bool running = false;
  unsigned long move_start_time = 0;
  int dir = 0;
  bool fullForward = false;
  bool fullBackward = false;
  bool calibrating = false;
};

class MotorController {
public:
  MotorController();
  void begin();
  
  void startMotor(int motor, int dir);
  void stopMotor(int motor);
  void stopAllMotors();
  void setMotorTarget(int motor, int target);
  void toggleFullForward(int motor);
  void toggleFullBackward(int motor);
  void toggleAllFullForward();
  void toggleAllFullBackward();
  void toggleCalibration(int motor);
  
  void updateMotors();
  bool checkLimitSwitch(int motor);
  
  Motor motors[4];
  Servo myServo1, myServo2;
  bool servoState = false;
  
  void setServoState(bool state);

private:
  void setupMotorPins();
};

extern MotorController motorCtrl;

#endif