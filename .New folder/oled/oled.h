#ifndef OLED_H
#define OLED_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "../config.h"
#include "../motor/motor.h"

class OLEDDisplay {
public:
  OLEDDisplay();
  bool begin();
  
  void drawMenu();
  void drawIPDisplay();
  
  void setMenuLevel(int level);
  void setMenuIndex(int level, int index);
  void setSelectedMotor(int motor);
  void setSelectedAction(int action);
  void setEditValue(bool edit);
  
  int getMenuLevel() const { return menu_level; }
  int getMenuIndex(int level) const { return menu_index[level]; }
  int getSelectedMotor() const { return selected_motor; }
  int getSelectedAction() const { return selected_action; }
  bool getEditValue() const { return edit_value; }
  
  void showIP(bool show) { showIP = show; }
  void setDisplayStartTime(unsigned long time) { displayStartTime = time; }
  
private:
  Adafruit_SSD1306 display;
  
  int menu_level = 0;
  int menu_index[7] = {0};
  int selected_motor = 0;
  int selected_action = 0;
  bool edit_value = false;
  
  bool showIP = true;
  unsigned long displayStartTime = 0;
};

extern OLEDDisplay oled;

#endif