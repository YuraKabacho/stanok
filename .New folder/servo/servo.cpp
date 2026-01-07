#include "servo.h"
#include "../../network/webserver.h"

const int ServoControl::servoPins[2] = {SERVO_PIN1, SERVO_PIN2};

void ServoControl::init() {
  // Servos are initialized when setState is called
}

void ServoControl::setState(bool state) {
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
  WebServer::sendState();
}