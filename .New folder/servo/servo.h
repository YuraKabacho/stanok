#define SERVO_H

#include "../../config.h"

class ServoControl {
public:
  static void init();
  static void setState(bool state);
  static bool getState() { return servoState; }
  
private:
  static const int servoPins[2];
};