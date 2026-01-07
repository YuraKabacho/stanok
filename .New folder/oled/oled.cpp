#include "oled.h"
#include "../motor/motor.h"

OLEDDisplay oled;

OLEDDisplay::OLEDDisplay() : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1) {}

bool OLEDDisplay::begin() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    return false;
  }
  return true;
}

void OLEDDisplay::drawIPDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println("ESP32 IP:");
  display.println("");
  
  display.setTextSize(2);
  if (WiFi.status() == WL_CONNECTED) {
    display.println(WiFi.localIP().toString());
    display.setTextSize(1);
    display.println("");
    display.println("Connected!");
    display.println("OTA Ready");
  } else {
    display.println("No WiFi");
    display.setTextSize(1);
    display.println("");
    display.println("AP Mode");
  }
  
  display.display();
}

void OLEDDisplay::drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Display header
  display.drawRect(0, 0, SCREEN_WIDTH, 14, SSD1306_WHITE);
  display.setCursor(4, 4);
  
  const char* headers[] = {
    "MAIN MENU", "MOTOR CONTROL TYPE", "MOTOR SELECT", 
    "ACTION SELECT", "DISTANCE CONTROL", "CALIBRATION", "SERVO CONTROL"
  };
  display.print(headers[menu_level]);

  // Display menu items
  display.setCursor(0, 16);

  switch (menu_level) {
    case 0: {
      const char* items[] = {"Motor Control", "Calibration", "Servo Control"};
      for (int i = 0; i < 3; i++) {
        if (i == menu_index[0]) {
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
          display.printf("> %s \n", items[i]);
          display.setTextColor(SSD1306_WHITE);
        } else {
          display.printf("  %s \n", items[i]);
        }
      }
      break;
    }
      
    case 1: {
      const char* items[] = {"All Motors", "Single Motor", "Back"};
      for (int i = 0; i < 3; i++) {
        if (i == menu_index[1]) {
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
          display.printf("> %s \n", items[i]);
          display.setTextColor(SSD1306_WHITE);
        } else {
          display.printf("  %s \n", items[i]);
        }
      }
      break;
    }
      
    case 2: {
      const char* items[] = {"Motor 0", "Motor 1", "Motor 2", "Motor 3", "Back"};
      for (int i = 0; i < 5; i++) {
        if (i == menu_index[2]) {
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
          display.printf("> %s \n", items[i]);
          display.setTextColor(SSD1306_WHITE);
        } else {
          display.printf("  %s \n", items[i]);
        }
      }
      break;
    }
      
    case 3: {
      const char* items[] = {"Distance Control", "Forward", "Backward", "Back"};
      for (int i = 0; i < 4; i++) {
        if (i == menu_index[3]) {
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
          if (i == 1) {
            display.printf("> Forward [%s] \n", motorCtrl.motors[selected_motor].fullForward ? "ON" : "OFF");
          } else if (i == 2) {
            display.printf("> Backward [%s] \n", motorCtrl.motors[selected_motor].fullBackward ? "ON" : "OFF");
          } else {
            display.printf("> %s \n", items[i]);
          }
          display.setTextColor(SSD1306_WHITE);
        } else {
          if (i == 1) {
            display.printf("  Forward [%s] \n", motorCtrl.motors[selected_motor].fullForward ? "ON" : "OFF");
          } else if (i == 2) {
            display.printf("  Backward [%s] \n", motorCtrl.motors[selected_motor].fullBackward ? "ON" : "OFF");
          } else {
            display.printf("  %s \n", items[i]);
          }
        }
      }
      break;
    }
      
    case 4: {
      if (menu_index[4] == 0 && edit_value) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.printf("> Target: [%d mm] \n", motorCtrl.motors[selected_motor].target);
        display.setTextColor(SSD1306_WHITE);
      } else if (menu_index[4] == 0) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.printf("> Target: %d mm \n", motorCtrl.motors[selected_motor].target);
        display.setTextColor(SSD1306_WHITE);
      } else {
        display.printf("  Target: %d mm \n", motorCtrl.motors[selected_motor].target);
      }

      if (menu_index[4] == 1) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.printf("> Current: %d mm \n", motorCtrl.motors[selected_motor].real_position);
        display.setTextColor(SSD1306_WHITE);
      } else {
        display.printf("  Current: %d mm \n", motorCtrl.motors[selected_motor].real_position);
      }

      if (menu_index[4] == 2) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.println("> Confirm");
        display.setTextColor(SSD1306_WHITE);
      } else {
        display.println("  Confirm");
      }

      if (menu_index[4] == 3) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.println("> Back");
        display.setTextColor(SSD1306_WHITE);
      } else {
        display.println("  Back");
      }
      break;
    }
      
    case 5: {
      const char* items[] = {"Cal. Motor 0", "Cal. Motor 1", "Cal. Motor 2", "Cal. Motor 3", "Back"};
      for (int i = 0; i < 5; i++) {
        if (i == menu_index[5]) {
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
          if (i < 4) {
            display.printf("> %s [%s]\n", items[i], motorCtrl.motors[i].calibrating ? "ON" : "OFF");
          } else {
            display.printf("> %s \n", items[i]);
          }
          display.setTextColor(SSD1306_WHITE);
        } else {
          if (i < 4) {
            display.printf("  %s [%s]\n", items[i], motorCtrl.motors[i].calibrating ? "ON" : "OFF");
          } else {
            display.printf("  %s \n", items[i]);
          }
        }
      }
      break;
    }
      
    case 6: {
      const char* items[] = {"Servo ON/OFF", "Back"};
      for (int i = 0; i < 2; i++) {
        if (i == menu_index[6]) {
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
          if (i == 0) {
            display.printf("> %s [%s]\n", items[i], motorCtrl.servoState ? "ON" : "OFF");
          } else {
            display.printf("> %s \n", items[i]);
          }
          display.setTextColor(SSD1306_WHITE);
        } else {
          if (i == 0) {
            display.printf("  %s [%s]\n", items[i], motorCtrl.servoState ? "ON" : "OFF");
          } else {
            display.printf("  %s \n", items[i]);
          }
        }
      }
      break;
    }
  }

  // Bottom status bar
  bool any_running = false;
  for (int i = 0; i < 4; i++) {
    if (motorCtrl.motors[i].running) {
      any_running = true;
      break;
    }
  }
  
  display.drawLine(0, SCREEN_HEIGHT-10, SCREEN_WIDTH, SCREEN_HEIGHT-10, SSD1306_WHITE);
  display.setCursor(4, SCREEN_HEIGHT-8);
  display.printf("Status: %s", any_running ? "RUNNING" : "STOPPED");

  display.display();
}

void OLEDDisplay::setMenuLevel(int level) {
  menu_level = level;
}

void OLEDDisplay::setMenuIndex(int level, int index) {
  if (level >= 0 && level < 7) {
    menu_index[level] = index;
  }
}

void OLEDDisplay::setSelectedMotor(int motor) {
  selected_motor = motor;
}

void OLEDDisplay::setSelectedAction(int action) {
  selected_action = action;
}

void OLEDDisplay::setEditValue(bool edit) {
  edit_value = edit;
}