#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Preferences.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiManager.h>

#include <ArduinoJson.h>
#include <ArduinoOTA.h>

#include <LittleFS.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

// ==== OTA Update Includes ====
#include <HTTPClient.h>
#include <Update.h>

// ==== OLED ====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==== Pin Definitions ====
const int encoderPins[] = {25, 33, 32}; // CLK, DT, SW
const int motorPins[4][2] = {
  {14, 15}, // M1: IN1, IN2
  {13, 12}, // M2: IN1, IN2
  {5, 23},  // M3: IN1, IN2
  {27, 26}  // M4: IN1, IN2
};
const int limitPins[] = {2, 4, 35, 34};
const int servoPins[] = {18, 19};

// ==== Parameters ====
const int max_mm = 20;
const int min_mm = 0;
const int ms_per_mm = 4.35 * 1000; // 4.35 секунди на 20 мм
#define ENCODER_DEBOUNCE 40

// ==== OTA Update Settings ====
const char* GITHUB_REPO = "YuraKabacho/stanok";
const char* FIRMWARE_FILENAME = "firmware.bin";
bool updateInProgress = false;
int updateProgress = 0;
String updateStatus = "";
unsigned long lastUpdateCheck = 0;
String latestVersion = "";

// ==== Global Variables ====
Servo myServo1, myServo2;
bool servoState = false;
int servo1Angle = 0;   // поточний кут серво 1
int servo2Angle = 0;   // поточний кут серво 2
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Motor structure
struct Motor {
  int manual_distance = 0;
  int real_position = 0;
  int target = 0;
  bool running = false;
  unsigned long move_start_time = 0;
  unsigned long last_position_update = 0;
  int dir = 0;
  bool fullForward = false;
  bool fullBackward = false;
  bool calibrating = false;
};

Motor motors[4];
Preferences preferences;           // для збереження позицій моторів
unsigned long lastSaveTime = 0;     // для періодичного збереження

// Menu variables
int menu_level = 0;
int menu_index[7] = {0};
int selected_motor = 0;
int selected_action = 0;
bool edit_value = false;

// Encoder variables
volatile int8_t encoder_delta = 0;
unsigned long lastEncoderUpdate = 0;
unsigned long lastDebounce = 0;
bool btnPressed = false;

// Display timer
unsigned long displayStartTime = 0;
bool showIP = true;

// WiFi Manager instance
WiFiManager wm;

// Function prototypes
void setServoState(bool state);
void moveServoSmooth(Servo &servo, int &currentAngle, int targetAngle, int stepDelay = 15);
void setMotorTarget(int motor, int target);
void drawMenu();
void drawHostnameDisplay();
void drawOTAProgress();
void sendState();
void toggleCalibration(int motor);
void startMotor(int motor, int dir);
void stopMotor(int motor);
void stopAllMotors();
void toggleFullForward(int motor);
void toggleFullBackward(int motor);
void toggleAllFullForward();
void toggleAllFullBackward();
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void IRAM_ATTR readEncoder();
void setupI2C();
void setupOTA();
void handleWebServer();
String checkForUpdate();
bool performUpdate(String firmwareUrl);
void sendUpdateStatus();
void loadMotorPositions();
void saveMotorPositions();

// ==== I2C ====
void setupI2C() {
  Wire.begin(21, 22); // SDA, SCL
  Wire.setClock(400000);
}

// ==== Preferences: збереження та завантаження позицій моторів ====
void loadMotorPositions() {
  preferences.begin("motors", false);
  for (int i = 0; i < 4; i++) {
    char key[10];
    sprintf(key, "pos%d", i);
    motors[i].real_position = preferences.getInt(key, 0);
    motors[i].target = motors[i].real_position; // за замовчуванням ціль = поточній позиції
  }
  preferences.end();
  Serial.println("Motor positions loaded from preferences");
}

void saveMotorPositions() {
  preferences.begin("motors", false);
  for (int i = 0; i < 4; i++) {
    char key[10];
    sprintf(key, "pos%d", i);
    preferences.putInt(key, motors[i].real_position);
  }
  preferences.end();
  lastSaveTime = millis();
  Serial.println("Motor positions saved to preferences");
}

// ==== OTA Update Functions ====
String checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    return "";
  }
  
  HTTPClient http;
  String url = "https://api.github.com/repos/" + String(GITHUB_REPO) + "/releases/latest";
  
  http.begin(url);
  http.setUserAgent("ESP32-OTA");
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    http.end();
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      return "";
    }
    
    String latestVer = doc["tag_name"].as<String>();
    latestVersion = latestVer;
    
    JsonArray assets = doc["assets"].as<JsonArray>();
    for (JsonObject asset : assets) {
      String name = asset["name"].as<String>();
      if (name == FIRMWARE_FILENAME) {
        String downloadUrl = asset["browser_download_url"].as<String>();
        return downloadUrl;
      }
    }
  }
  
  http.end();
  return "";
}

bool performUpdate(String firmwareUrl) {
  if (WiFi.status() != WL_CONNECTED) {
    updateStatus = "WiFi not connected";
    return false;
  }
  
  HTTPClient http;
  
  http.begin(firmwareUrl);
  http.setUserAgent("ESP32-OTA");
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    
    if (contentLength > 0) {
      if (contentLength > (ESP.getFreeSketchSpace() - 0x1000)) {
        updateStatus = "Not enough space";
        http.end();
        return false;
      }
      
      if (!Update.begin(contentLength)) {
        updateStatus = "Update begin failed: " + String(Update.getError());
        http.end();
        return false;
      }
      
      WiFiClient *stream = http.getStreamPtr();
      uint8_t buffer[1024];
      size_t totalRead = 0;
      
      while (http.connected() && totalRead < contentLength) {
        size_t read = stream->readBytes(buffer, min(sizeof(buffer), contentLength - totalRead));
        if (read > 0) {
          Update.write(buffer, read);
          totalRead += read;
          
          updateProgress = (totalRead * 100) / contentLength;
          sendUpdateStatus();
          
          if (millis() - lastEncoderUpdate > 500) {
            drawOTAProgress();
            lastEncoderUpdate = millis();
          }
        }
      }
      
      if (Update.end()) {
        updateStatus = "Update complete! Restarting...";
        updateProgress = 100;
        sendUpdateStatus();
        http.end();
        
        delay(2000);
        ESP.restart();
        return true;
      } else {
        updateStatus = "Update failed: " + String(Update.getError());
        http.end();
        return false;
      }
    }
  } else {
    updateStatus = "HTTP error: " + String(httpCode);
    http.end();
    return false;
  }
  
  http.end();
  return false;
}

// Функція для задачі OTA (отримує String* через параметр)
void otaTask(void *param) {
  String* urlPtr = (String*)param;
  performUpdate(*urlPtr);
  delete urlPtr;
  updateInProgress = false;
  vTaskDelete(NULL);
}

void drawOTAProgress() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println("OTA UPDATE");
  display.println("==========");
  
  display.setCursor(0, 20);
  display.println(updateStatus);
  
  int barWidth = SCREEN_WIDTH - 4;
  int barHeight = 10;
  int barX = 2;
  int barY = SCREEN_HEIGHT - barHeight - 10;
  
  display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
  
  int progressWidth = (updateProgress * (barWidth - 2)) / 100;
  display.fillRect(barX + 1, barY + 1, progressWidth, barHeight - 2, SSD1306_WHITE);
  
  display.setCursor(SCREEN_WIDTH/2 - 10, barY - 12);
  display.printf("%d%%", updateProgress);
  
  display.display();
}

void sendUpdateStatus() {
  JsonDocument doc;
  doc["type"] = "update_status";
  doc["status"] = updateStatus;
  doc["progress"] = updateProgress;
  doc["inProgress"] = updateInProgress;
  doc["latestVersion"] = latestVersion;
  
  String output;
  serializeJson(doc, output);
  ws.textAll(output);
}

// ==== Плавний рух серво ====
void moveServoSmooth(Servo &servo, int &currentAngle, int targetAngle, int stepDelay) {
  if (!servo.attached()) return;
  int step = (targetAngle > currentAngle) ? 1 : -1;
  while (currentAngle != targetAngle) {
    currentAngle += step;
    servo.write(currentAngle);
    delay(stepDelay);
  }
}

// ==== Servo Control Function ====
void setServoState(bool state) {
  servoState = state;
  if (servoState) {
    // Вмикаємо: спочатку перший серво
    if (!myServo1.attached()) {
      myServo1.attach(servoPins[0], 500, 2400);
    }
    moveServoSmooth(myServo1, servo1Angle, 180, 15); // повільно до 180

    delay(500); // затримка 500 мс між увімкненням

    // Другий серво (віддзеркалений) – крутимо в протилежний бік (0 градусів)
    if (!myServo2.attached()) {
      myServo2.attach(servoPins[1], 500, 2400);
    }
    moveServoSmooth(myServo2, servo2Angle, 0, 15);   // повільно до 0
  } else {
    // Вимикаємо одночасно – просто детачимо
    myServo1.detach();
    myServo2.detach();
  }
  sendState();
}

// ==== OLED Display ====
void drawHostnameDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println("Hostname:");
  display.println("");
  
  display.setTextSize(2);
  display.println(WiFi.getHostname());
  display.setTextSize(1);
  display.println("");
  display.println("stanok.local");
  display.display();
}

void drawMenu() {
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
            display.printf("> Forward [%s] \n", motors[selected_motor].fullForward ? "ON" : "OFF");
          } else if (i == 2) {
            display.printf("> Backward [%s] \n", motors[selected_motor].fullBackward ? "ON" : "OFF");
          } else {
            display.printf("> %s \n", items[i]);
          }
          display.setTextColor(SSD1306_WHITE);
        } else {
          if (i == 1) {
            display.printf("  Forward [%s] \n", motors[selected_motor].fullForward ? "ON" : "OFF");
          } else if (i == 2) {
            display.printf("  Backward [%s] \n", motors[selected_motor].fullBackward ? "ON" : "OFF");
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
        display.printf("> Target: [%d mm] \n", motors[selected_motor].target);
        display.setTextColor(SSD1306_WHITE);
      } else if (menu_index[4] == 0) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.printf("> Target: %d mm \n", motors[selected_motor].target);
        display.setTextColor(SSD1306_WHITE);
      } else {
        display.printf("  Target: %d mm \n", motors[selected_motor].target);
      }

      if (menu_index[4] == 1) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.printf("> Current: %d mm \n", motors[selected_motor].real_position);
        display.setTextColor(SSD1306_WHITE);
      } else {
        display.printf("  Current: %d mm \n", motors[selected_motor].real_position);
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
            display.printf("> %s [%s]\n", items[i], motors[i].calibrating ? "ON" : "OFF");
          } else {
            display.printf("> %s \n", items[i]);
          }
          display.setTextColor(SSD1306_WHITE);
        } else {
          if (i < 4) {
            display.printf("  %s [%s]\n", items[i], motors[i].calibrating ? "ON" : "OFF");
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
            display.printf("> %s [%s]\n", items[i], servoState ? "ON" : "OFF");
          } else {
            display.printf("> %s \n", items[i]);
          }
          display.setTextColor(SSD1306_WHITE);
        } else {
          if (i == 0) {
            display.printf("  %s [%s]\n", items[i], servoState ? "ON" : "OFF");
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
    if (motors[i].running) {
      any_running = true;
      break;
    }
  }
  
  display.drawLine(0, SCREEN_HEIGHT-10, SCREEN_WIDTH, SCREEN_HEIGHT-10, SSD1306_WHITE);
  display.setCursor(4, SCREEN_HEIGHT-8);
  display.printf("Status: %s", any_running ? "RUNNING" : "STOPPED");

  display.display();
}

// ==== Encoder ====
void IRAM_ATTR readEncoder() {
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

// ==== Motor Control ====
void startMotor(int motor, int dir) {
  if (motor < 0 || motor > 3) return;
  
  motors[motor].running = true;
  motors[motor].dir = dir;
  motors[motor].move_start_time = millis();
  motors[motor].last_position_update = millis();
  
  digitalWrite(motorPins[motor][0], dir > 0 ? HIGH : LOW);
  digitalWrite(motorPins[motor][1], dir < 0 ? HIGH : LOW);
  
  Serial.printf("Motor %d started, direction: %d\n", motor, dir);
}

void stopMotor(int motor) {
  if (motor < 0 || motor > 3) return;
  
  motors[motor].running = false;
  motors[motor].fullForward = false;
  motors[motor].fullBackward = false;
  motors[motor].calibrating = false;
  
  digitalWrite(motorPins[motor][0], LOW);
  digitalWrite(motorPins[motor][1], LOW);
  
  // Зберігаємо позицію після зупинки
  saveMotorPositions();
  
  Serial.printf("Motor %d stopped\n", motor);
  
  sendState();
  drawMenu();
}

void stopAllMotors() {
  for (int i = 0; i < 4; i++) {
    stopMotor(i);
  }
}

void toggleFullForward(int motor) {
  if (motors[motor].fullForward) {
    stopMotor(motor);
  } else {
    motors[motor].calibrating = false;
    motors[motor].fullBackward = false;
    motors[motor].fullForward = true;
    startMotor(motor, 1);
  }
  sendState();
  drawMenu();
}

void toggleFullBackward(int motor) {
  if (motors[motor].fullBackward) {
    stopMotor(motor);
  } else {
    motors[motor].calibrating = false;
    motors[motor].fullForward = false;
    motors[motor].fullBackward = true;
    startMotor(motor, -1);
  }
  sendState();
  drawMenu();
}

void toggleAllFullForward() {
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
  sendState();
}

void toggleAllFullBackward() {
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
  sendState();
}

void toggleCalibration(int motor) {
  if (motors[motor].calibrating) {
    stopMotor(motor);
  } else {
    motors[motor].fullForward = false;
    motors[motor].fullBackward = false;
    motors[motor].calibrating = true;
    startMotor(motor, -1);
  }
  sendState();
  drawMenu();
}

// WebSocket event handler
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch(type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      sendState();
      break;
      
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
      
    case WS_EVT_DATA: {
      AwsFrameInfo *info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data);
        
        if (error) {
          Serial.print(F("deserializeJson() failed: "));
          Serial.println(error.f_str());
          return;
        }
        
        const char* commandType = doc["type"];
        JsonObject dataObj = doc["data"];
        
        if (strcmp(commandType, "set_target") == 0) {
          int motor = dataObj["motor"];
          int target = dataObj["target"];
          setMotorTarget(motor, target);
        }
        else if (strcmp(commandType, "calibrate") == 0) {
          int motor = dataObj["motor"];
          toggleCalibration(motor);
        }
        else if (strcmp(commandType, "set_all_targets") == 0) {
          int target = dataObj["target"];
          for (int i = 0; i < 4; i++) {
            setMotorTarget(i, target);
          }
        }
        else if (strcmp(commandType, "calibrate_all") == 0) {
          for (int i = 0; i < 4; i++) {
            toggleCalibration(i);
          }
        }
        else if (strcmp(commandType, "emergency_stop") == 0) {
          stopAllMotors();
        }
        else if (strcmp(commandType, "set_servo") == 0) {
          bool state = dataObj["state"];
          setServoState(state);
        }
        else if (strcmp(commandType, "full_forward") == 0) {
          int motor = dataObj["motor"];
          toggleFullForward(motor);
        }
        else if (strcmp(commandType, "full_backward") == 0) {
          int motor = dataObj["motor"];
          toggleFullBackward(motor);
        }
        else if (strcmp(commandType, "all_full_forward") == 0) {
          toggleAllFullForward();
        }
        else if (strcmp(commandType, "all_full_backward") == 0) {
          toggleAllFullBackward();
        }
        else if (strcmp(commandType, "get_ip") == 0) {
          sendState();
        }
        else if (strcmp(commandType, "check_update") == 0) {
          String firmwareUrl = checkForUpdate();
          if (firmwareUrl != "") {
            updateStatus = "Update found! Starting...";
            updateInProgress = true;
            sendUpdateStatus();
            
            // Передаємо URL через копію в купі
            String* urlPtr = new String(firmwareUrl);
            xTaskCreate(otaTask, "OTA Task", 8192, urlPtr, 1, NULL);
          } else {
            updateStatus = "No update available";
            sendUpdateStatus();
          }
        }
        else if (strcmp(commandType, "perform_update") == 0) {
          String firmwareUrl = dataObj["url"].as<String>();
          if (firmwareUrl != "") {
            updateStatus = "Starting update...";
            updateInProgress = true;
            sendUpdateStatus();
            
            String* urlPtr = new String(firmwareUrl);
            xTaskCreate(otaTask, "OTA Task", 8192, urlPtr, 1, NULL);
          }
        }
        else if (strcmp(commandType, "restart") == 0) {
          ESP.restart();
        }
        else if (strcmp(commandType, "reset_wifi") == 0) {
          wm.resetSettings();
          ESP.restart();
        }
      }
      break;
    }
      
    case WS_EVT_ERROR:
      Serial.printf("WebSocket error\n");
      break;
  }
}

// Send state to all WebSocket clients
void sendState() {
  JsonDocument doc;
  
  for (int i = 0; i < 4; i++) {
    char motorKey[10];
    sprintf(motorKey, "motor%d", i);
    
    // Виправлено deprecated createNestedObject
    JsonObject motorData = doc[motorKey].to<JsonObject>();
    motorData["position"] = motors[i].real_position;
    motorData["target"] = motors[i].target;
    motorData["running"] = motors[i].running;
    motorData["calibrating"] = motors[i].calibrating;
    motorData["fullForward"] = motors[i].fullForward;
    motorData["fullBackward"] = motors[i].fullBackward;
  }
  
  doc["servoState"] = servoState;
  doc["ip"] = WiFi.localIP().toString();
  doc["updateInProgress"] = updateInProgress;
  doc["updateProgress"] = updateProgress;
  doc["updateStatus"] = updateStatus;
  doc["latestVersion"] = latestVersion;
  
  bool any_running = false;
  for (int i = 0; i < 4; i++) {
    if (motors[i].running) {
      any_running = true;
      break;
    }
  }
  doc["globalStatus"] = any_running ? "RUNNING" : "STOPPED";
  
  String output;
  serializeJson(doc, output);
  
  ws.textAll(output);
}

void setMotorTarget(int motor, int target) {
  if (motor < 0 || motor > 3) return;
  
  motors[motor].target = target;
  Serial.printf("Setting motor %d target to %d, current position: %d\n", 
                motor, target, motors[motor].real_position);
  
  motors[motor].running = true;
  
  if (motors[motor].target > motors[motor].real_position) {
    startMotor(motor, 1);
  } else if (motors[motor].target < motors[motor].real_position) {
    startMotor(motor, -1);
  } else {
    stopMotor(motor);
  }
  
  drawMenu();
  sendState();
}

void setupOTA() {
  ArduinoOTA.setHostname("stanok");
  ArduinoOTA.setPassword("ota123");

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else
        type = "filesystem";

      Serial.println("Start updating " + type);
      stopAllMotors();
      
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("OTA UPDATE");
      display.println("Updating " + type);
      display.display();
    })
    .onEnd([]() {
      Serial.println("\nEnd");
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("Update Complete!");
      display.println("Rebooting...");
      display.display();
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("OTA UPDATE");
      display.printf("Progress: %u%%", (progress / (total / 100)));
      display.display();
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
      
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("OTA ERROR!");
      display.printf("Error: %u", error);
      display.display();
    });

  ArduinoOTA.begin();
  Serial.println("OTA Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void handleWebServer() {
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  
  server.on("/admin", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/admin.html", "text/html");
  });
  
  server.on("/style.css", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/style.css", "text/css");
  });
  
  server.on("/admin-style.css", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/admin-style.css", "text/css");
  });
  
  server.on("/script.js", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/script.js", "application/javascript");
  });
  
  server.on("/admin-script.js", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/admin-script.js", "application/javascript");
  });

  server.begin();
  Serial.println("HTTP server started");
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nBooting...");
  setupI2C();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
    for (;;);
  }

  for (int i = 0; i < 3; i++) {
    pinMode(encoderPins[i], INPUT_PULLUP);
  }
  attachInterrupt(digitalPinToInterrupt(encoderPins[0]), readEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(encoderPins[1]), readEncoder, CHANGE);

  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 2; j++) {
      pinMode(motorPins[i][j], OUTPUT);
      digitalWrite(motorPins[i][j], LOW);
    }
  }
  
  for (int i = 0; i < 4; i++) {
    pinMode(limitPins[i], INPUT_PULLUP);
  }
  
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    Serial.println("Formatting LittleFS...");
    LittleFS.format();
    if (!LittleFS.begin()) {
      Serial.println("LittleFS mount failed after formatting");
    }
  }
  Serial.println("LittleFS mounted successfully");

  // Ініціалізація моторів
  for (int i = 0; i < 4; i++) {
    motors[i].manual_distance = 0;
    motors[i].target = 0;
    motors[i].running = false;
    motors[i].fullForward = false;
    motors[i].fullBackward = false;
    motors[i].calibrating = false;
    Serial.printf("Motor %d initialized\n", i);
  }

  // Завантажуємо збережені позиції
  loadMotorPositions();

  wm.setConfigPortalTimeout(300); // 5 хвилин
  wm.setHostname("stanok");
  
  bool res = wm.autoConnect("ESP32", "12345678");
  
  if (!res) {
    Serial.println("Failed to connect, starting config portal...");
    // Якщо не підключились, запускаємо портал (буде блокувати)
    wm.startConfigPortal("ESP32", "12345678");
  }
  
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Hostname: ");
  Serial.println(WiFi.getHostname());

  displayStartTime = millis();
  drawHostnameDisplay();  // показуємо hostname замість IP

  setupOTA();
  handleWebServer();

  setServoState(false);
  
  Serial.println("Setup complete!");
}

void loop() {
  ArduinoOTA.handle();
  
  if (updateInProgress) {
    delay(100);
    return;
  }
  
  if (millis() - displayStartTime < 10000) {
    if (showIP) {
      drawHostnameDisplay();
    }
    if (millis() - displayStartTime >= 10000 && showIP) {
      showIP = false;
      drawMenu();
    }
  } else {
    // Handle encoder scrolling
    if (encoder_delta != 0) {
      if (millis() - lastEncoderUpdate > ENCODER_DEBOUNCE) {
        lastEncoderUpdate = millis();
        
        int8_t delta = (encoder_delta > 0) ? 1 : -1;
        
        if (menu_level == 4 && edit_value) {
          motors[selected_motor].target += delta;
          if (motors[selected_motor].target < min_mm) motors[selected_motor].target = min_mm;
          if (motors[selected_motor].target > max_mm) motors[selected_motor].target = max_mm;
        } else {
          menu_index[menu_level] += delta;
          
          int max_indices[] = {2, 2, 4, 3, 3, 4, 1};
          menu_index[menu_level] = constrain(menu_index[menu_level], 0, max_indices[menu_level]);
        }
        
        encoder_delta = 0;
        drawMenu();
        sendState();
      }
    }

    bool btnState = digitalRead(encoderPins[2]);
    if (btnState == LOW && !btnPressed && millis() - lastDebounce > 300) {
      btnPressed = true;
      lastDebounce = millis();

      switch (menu_level) {
        case 0:
          if (menu_index[0] == 0) {
            menu_level = 1;
            menu_index[1] = 0;
          } else if (menu_index[0] == 1) {
            menu_level = 5;
            menu_index[5] = 0;
          } else if (menu_index[0] == 2) {
            menu_level = 6;
            menu_index[6] = 0;
          }
          break;
          
        case 1:
          if (menu_index[1] == 0) {
            menu_level = 3;
            menu_index[3] = 0;
            selected_motor = -1;
          } else if (menu_index[1] == 1) {
            menu_level = 2;
            menu_index[2] = 0;
          } else if (menu_index[1] == 2) {
            menu_level = 0;
          }
          break;
          
        case 2:
          if (menu_index[2] == 4) {
            menu_level = 1;
          } else {
            selected_motor = menu_index[2];
            menu_level = 3;
            menu_index[3] = 0;
          }
          break;
          
        case 3:
          selected_action = menu_index[3];
          if (selected_action == 3) {
            menu_level = (selected_motor == -1) ? 1 : 2;
          } 
          else if (selected_action == 0) {
            menu_level = 4;
            menu_index[4] = 0;
            edit_value = false;
          } 
          else if (selected_action == 1) {
            if (selected_motor == -1) {
              toggleAllFullForward();
            } else {
              toggleFullForward(selected_motor);
            }
          }
          else if (selected_action == 2) {
            if (selected_motor == -1) {
              toggleAllFullBackward();
            } else {
              toggleFullBackward(selected_motor);
            }
          }
          break;
          
        case 4:
          if (menu_index[4] == 0) {
            edit_value = !edit_value;
          } 
          else if (menu_index[4] == 2) {
            if (selected_motor == -1) {
              for (int i = 0; i < 4; i++) {
                setMotorTarget(i, motors[i].target);
              }
            } else {
              setMotorTarget(selected_motor, motors[selected_motor].target);
            }
          } 
          else if (menu_index[4] == 3) {
            menu_level = 3;
            edit_value = false;
          }
          break;
          
        case 5:
          if (menu_index[5] == 4) {
            menu_level = 0;
          } else {
            toggleCalibration(menu_index[5]);
          }
          break;
          
        case 6:
          if (menu_index[6] == 0) {
            setServoState(!servoState);
          } 
          else if (menu_index[6] == 1) {
            menu_level = 0;
          }
          break;
      }
      
      drawMenu();
      sendState();
    } else if (btnState == HIGH && btnPressed) {
      btnPressed = false;
    }

    // Check limit switches
    for (int i = 0; i < 4; i++) {
      if (motors[i].calibrating && digitalRead(limitPins[i]) == LOW) {
        stopMotor(i);
        motors[i].manual_distance = 0;
        motors[i].real_position = 0;
        sendState();
        drawMenu();
        saveMotorPositions(); // зберігаємо після калібрування
      }
    }

    // Update motor positions
    unsigned long current_time = millis();
    for (int i = 0; i < 4; i++) {
      if (motors[i].running && !motors[i].fullForward && !motors[i].fullBackward) {
        if (current_time - motors[i].last_position_update >= ms_per_mm) {
          motors[i].last_position_update = current_time;
          
          if (motors[i].dir > 0) {
            motors[i].real_position++;
          } else if (motors[i].dir < 0) {
            // Не дозволяємо позиції стати від'ємною під час калібрування
            if (motors[i].real_position > 0) {
              motors[i].real_position--;
            }
          }

          if (motors[i].dir > 0) {
            motors[i].manual_distance++;
          } else if (motors[i].dir < 0) {
            motors[i].manual_distance--;
          }
          
          // Перевірка досягнення цілі
          if (!motors[i].calibrating) {
            if ((motors[i].dir > 0 && motors[i].manual_distance >= motors[i].target) ||
                (motors[i].dir < 0 && motors[i].manual_distance <= motors[i].target)) {
              stopMotor(i);
            }
          }
          
          // Періодичне збереження (раз на 5 секунд, якщо мотор рухається)
          if (current_time - lastSaveTime > 5000) {
            saveMotorPositions();
          }
          
          sendState();
          drawMenu();
        }
      }
    }
  }
  
  delay(10);
}