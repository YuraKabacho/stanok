#include "motor.h"
#include "../oled/oled.h"

MotorController motorCtrl;

MotorController::MotorController() {}

void MotorController::begin() {
  setupMotorPins();
  setServoState(false);
}

void MotorController::setupMotorPins() {
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 2; j++) {
      pinMode(motorPins[i][j], OUTPUT);
      digitalWrite(motorPins[i][j], LOW);
    }
  }
  
  for (int i = 0; i < 4; i++) {
    pinMode(limitPins[i], INPUT_PULLUP);
  }
}

void MotorController::startMotor(int motor, int dir) {
  if (motor < 0 || motor > 3) return;
  
  motors[motor].running = true;
  motors[motor].dir = dir;
  motors[motor].move_start_time = millis();
  
  digitalWrite(motorPins[motor][0], dir > 0 ? HIGH : LOW);
  digitalWrite(motorPins[motor][1], dir < 0 ? HIGH : LOW);
}

void MotorController::stopMotor(int motor) {
  if (motor < 0 || motor > 3) return;
  
  motors[motor].running = false;
  motors[motor].fullForward = false;
  motors[motor].fullBackward = false;
  motors[motor].calibrating = false;
  
  digitalWrite(motorPins[motor][0], LOW);
  digitalWrite(motorPins[motor][1], LOW);
}

void MotorController::stopAllMotors() {
  for (int i = 0; i < 4; i++) {
    stopMotor(i);
  }
}

void MotorController::setMotorTarget(int motor, int target) {
  if (motor < 0 || motor > 3) return;
  
  motors[motor].target = target;
  motors[motor].running = true;
  
  if (motors[motor].target > motors[motor].real_position) {
    startMotor(motor, 1);
  } else if (motors[motor].target < motors[motor].real_position) {
    startMotor(motor, -1);
  } else {
    stopMotor(motor);
  }
}

void MotorController::toggleFullForward(int motor) {
  if (motors[motor].fullForward) {
    stopMotor(motor);
  } else {
    motors[motor].calibrating = false;
    motors[motor].fullBackward = false;
    motors[motor].fullForward = true;
    startMotor(motor, 1);
  }
}

void MotorController::toggleFullBackward(int motor) {
  if (motors[motor].fullBackward) {
    stopMotor(motor);
  } else {
    motors[motor].calibrating = false;
    motors[motor].fullForward = false;
    motors[motor].fullBackward = true;
    startMotor(motor, -1);
  }
}

void MotorController::toggleAllFullForward() {
  bool allRunning = true;
  for (int i = 0; i < 4; i++) {
    if (!motors[i].fullForward) allRunning = false;
  }
  
  for (int i = 0; i < 4; i++) {
    if (allRunning) {
      stopMotor(i);
    } else {
      motors[i].calibrating = false;
      motors[i].fullBackward = false;
      motors[i].fullForward = true;
      startMotor(i, 1);
    }
  }
}

void MotorController::toggleAllFullBackward() {
  bool allRunning = true;
  for (int i = 0; i < 4; i++) {
    if (!motors[i].fullBackward) allRunning = false;
  }
  
  for (int i = 0; i < 4; i++) {
    if (allRunning) {
      stopMotor(i);
    } else {
      motors[i].calibrating = false;
      motors[i].fullForward = false;
      motors[i].fullBackward = true;
      startMotor(i, -1);
    }
  }
}

void MotorController::toggleCalibration(int motor) {
  if (motors[motor].calibrating) {
    stopMotor(motor);
  } else {
    motors[motor].fullForward = false;
    motors[motor].fullBackward = false;
    motors[motor].calibrating = true;
    startMotor(motor, -1);
  }
}

void MotorController::setServoState(bool state) {
  servoState = state;
  if (servoState) {
    if (!myServo1.attached()) {
      myServo1.attach(servoPins[0], 500, 2400);
    }
    if (!myServo2.attached()) {
      myServo2.attach(servoPins[1], 500, 2400);
    }
    myServo1.write(180);
    myServo2.write(180);
  } else {
    myServo1.detach();
    myServo2.detach();
  }
}

void MotorController::updateMotors() {
  unsigned long current_time = millis();
  for (int i = 0; i < 4; i++) {
    if (motors[i].running) {
      if (current_time - motors[i].move_start_time >= ms_per_mm) {
        motors[i].move_start_time = current_time;
        
        if (motors[i].dir > 0) {
          motors[i].real_position++;
        } else if (motors[i].dir < 0) {
          motors[i].real_position--;
        }

        if (!motors[i].fullForward && !motors[i].fullBackward && !motors[i].calibrating) {
          if (motors[i].dir > 0) {
            motors[i].manual_distance++;
          } else if (motors[i].dir < 0) {
            motors[i].manual_distance--;
          }
          
          if ((motors[i].dir > 0 && motors[i].manual_distance >= motors[i].target) ||
              (motors[i].dir < 0 && motors[i].manual_distance <= motors[i].target)) {
            stopMotor(i);
          }
        }
      }
    }
  }
}

bool MotorController::checkLimitSwitch(int motor) {
  return digitalRead(limitPins[motor]) == HIGH;
}