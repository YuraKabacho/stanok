#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

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
static const int encoderPins[] = {25, 33, 32}; // CLK, DT, SW
static const int motorPins[4][2] = {
  {15, 14}, // M1: IN1, IN2
  {13, 12}, // M2: IN1, IN2
  {27, 26}, // M3: IN1, IN2
  {5, 23}   // M4: IN1, IN2
};
static const int limitPins[] = {2, 4, 35, 34};
static const int servoPins[] = {18, 19};

// ==== Parameters ====
static const int max_mm = 20;
static const int min_mm = 0;
static const int ms_per_mm = 6800;
static const uint32_t ENCODER_DEBOUNCE = 40;
static const uint32_t UPDATE_CHECK_INTERVAL = 300000; // 5 minutes

// ==== OTA Update Settings ====
const char* GITHUB_REPO = "YuraKabacho/stanok";
const char* FIRMWARE_FILENAME = "firmware.bin";
const char* LITTLEFS_FILENAME = "littlefs.bin";

// ==== Global Variables ====
Servo myServo1, myServo2;
bool servoState = false;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Motor structure
struct Motor {
  int manual_distance = 0;
  int real_position = 0;
  int target = 0;
  bool running = false;
  uint32_t move_start_time = 0;
  uint32_t last_position_update = 0;
  int8_t dir = 0;
  bool fullForward = false;
  bool fullBackward = false;
  bool calibrating = false;
};

Motor motors[4];

// Menu variables
int8_t menu_level = 0;
int8_t menu_index[7] = {0};
int8_t selected_motor = 0;
int8_t selected_action = 0;
bool edit_value = false;

// Encoder variables
volatile int8_t encoder_delta = 0;
uint32_t lastEncoderUpdate = 0;
uint32_t lastDebounce = 0;
bool btnPressed = false;

// Display timer
uint32_t displayStartTime = 0;
bool showIP = true;

// WiFi Manager instance
WiFiManager wm;

// Update variables
enum UpdateType { UPDATE_NONE, UPDATE_FIRMWARE, UPDATE_LITTLEFS };
UpdateType currentUpdateType = UPDATE_NONE;
bool updateInProgress = false;
int updateProgress = 0;
String updateStatus = "";
uint32_t lastUpdateCheck = 0;
String latestVersion = "";
String latestFirmwareUrl = "";
String latestLittleFSUrl = "";

// Function prototypes
void setServoState(bool state);
void setMotorTarget(int motor, int target);
void drawMenu();
void drawIPDisplay();
void drawOTAProgress();
void sendState();
void toggleCalibration(int motor);
void startMotor(int motor, int8_t dir);
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
String checkForUpdates();
bool performUpdate(String url, UpdateType type);
void sendUpdateStatus();

// ==== I2C ====
void setupI2C() {
  Wire.begin(21, 22); // SDA, SCL
  Wire.setClock(400000);
}

// ==== OTA Update Functions ====
String checkForUpdates() {
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
    
    latestVersion = doc["tag_name"].as<String>();
    latestFirmwareUrl = "";
    latestLittleFSUrl = "";
    
    // Find firmware.bin and littlefs.bin assets
    JsonArray assets = doc["assets"].as<JsonArray>();
    for (JsonObject asset : assets) {
      String name = asset["name"].as<String>();
      String downloadUrl = asset["browser_download_url"].as<String>();
      
      if (name == FIRMWARE_FILENAME) {
        latestFirmwareUrl = downloadUrl;
      } else if (name == LITTLEFS_FILENAME) {
        latestLittleFSUrl = downloadUrl;
      }
    }
    
    // Return JSON with both URLs
    JsonDocument responseDoc;
    responseDoc["firmware_url"] = latestFirmwareUrl;
    responseDoc["littlefs_url"] = latestLittleFSUrl;
    responseDoc["latest_version"] = latestVersion;
    
    String response;
    serializeJson(responseDoc, response);
    return response;
  }
  
  http.end();
  return "";
}

bool performUpdate(String url, UpdateType type) {
  if (WiFi.status() != WL_CONNECTED) {
    updateStatus = "WiFi not connected";
    return false;
  }
  
  HTTPClient http;
  http.begin(url);
  http.setUserAgent("ESP32-OTA");
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    
    if (contentLength <= 0) {
      updateStatus = "Invalid content length";
      http.end();
      return false;
    }
    
    // Check if we have enough space
    if (type == UPDATE_FIRMWARE && contentLength > (ESP.getFreeSketchSpace() - 0x1000)) {
      updateStatus = "Not enough space for firmware";
      http.end();
      return false;
    }
    
    // Start update
    int cmd = (type == UPDATE_FIRMWARE) ? U_FLASH : U_SPIFFS;
    if (!Update.begin(contentLength, cmd)) {
      updateStatus = "Update begin failed: " + String(Update.getError());
      http.end();
      return false;
    }
    
    // Get update stream
    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[2048]; // Increased buffer size
    size_t totalRead = 0;
    uint32_t lastProgressUpdate = 0;
    
    while (http.connected() && totalRead < (size_t)contentLength) {
      size_t toRead = min(sizeof(buffer), (size_t)contentLength - totalRead);
      size_t read = stream->readBytes(buffer, toRead);
      
      if (read > 0) {
        Update.write(buffer, read);
        totalRead += read;
        
        // Update progress every 2%
        int newProgress = (totalRead * 100) / contentLength;
        if (newProgress > updateProgress || millis() - lastProgressUpdate > 500) {
          updateProgress = newProgress;
          lastProgressUpdate = millis();
          sendUpdateStatus();
          
          // Update display less frequently
          if (millis() - lastEncoderUpdate > 500) {
            drawOTAProgress();
            lastEncoderUpdate = millis();
          }
        }
      }
      
      // Small delay to prevent watchdog trigger
      delay(1);
    }
    
    http.end();
    
    if (Update.end()) {
      updateStatus = (type == UPDATE_FIRMWARE) ? "Firmware update complete! Restarting..." : "LittleFS update complete!";
      updateProgress = 100;
      sendUpdateStatus();
      
      if (type == UPDATE_FIRMWARE) {
        delay(2000);
        ESP.restart();
      }
      return true;
    } else {
      updateStatus = "Update failed: " + String(Update.getError());
      return false;
    }
  } else {
    updateStatus = "HTTP error: " + String(httpCode);
    http.end();
    return false;
  }
}

void drawOTAProgress() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println(currentUpdateType == UPDATE_FIRMWARE ? "FIRMWARE UPDATE" : "LITTLEFS UPDATE");
  display.println("==========");
  
  // Handle long status messages
  String displayStatus = updateStatus;
  if (displayStatus.length() > 21) {
    displayStatus = displayStatus.substring(0, 21);
  }
  display.setCursor(0, 20);
  display.println(displayStatus);
  
  // Progress bar
  int barWidth = SCREEN_WIDTH - 4;
  int barHeight = 10;
  int barX = 2;
  int barY = SCREEN_HEIGHT - barHeight - 10;
  
  display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
  int progressWidth = (updateProgress * (barWidth - 2)) / 100;
  display.fillRect(barX + 1, barY + 1, progressWidth, barHeight - 2, SSD1306_WHITE);
  
  // Draw percentage
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
  doc["updateType"] = (currentUpdateType == UPDATE_FIRMWARE) ? "firmware" : "littlefs";
  
  String output;
  serializeJson(doc, output);
  ws.textAll(output);
}

// ==== Servo Control Function ====
void setServoState(bool state) {
  servoState = state;
  if (servoState) {
    if (!myServo1.attached()) myServo1.attach(servoPins[0], 500, 2400);
    if (!myServo2.attached()) myServo2.attach(servoPins[1], 500, 2400);
    myServo1.write(180);
    myServo2.write(180);
  } else {
    myServo1.detach();
    myServo2.detach();
  }
  sendState();
}

// ==== OLED Display ====
void drawIPDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println("ESP32 IP:");
  display.println("");
  
  display.setTextSize(2);
  if (WiFi.status() == WL_CONNECTED) {
    display.println(WiFi.localIP().toString());
  } else {
    display.println("No WiFi");
  }
  
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.println(WiFi.status() == WL_CONNECTED ? "Connected! OTA Ready" : "AP Mode");
  
  display.display();
}

void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Display header
  display.drawRect(0, 0, SCREEN_WIDTH, 14, SSD1306_WHITE);
  display.setCursor(4, 4);
  
  static const char* headers[] = {
    "MAIN MENU", "MOTOR CONTROL TYPE", "MOTOR SELECT", 
    "ACTION SELECT", "DISTANCE CONTROL", "CALIBRATION", "SERVO CONTROL"
  };
  
  if (menu_level < 7) {
    display.print(headers[menu_level]);
  }

  // Display menu items
  display.setCursor(0, 16);

  switch (menu_level) {
    case 0: {
      static const char* items[] = {"Motor Control", "Calibration", "Servo Control"};
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
      static const char* items[] = {"All Motors", "Single Motor", "Back"};
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
      static const char* items[] = {"Motor 0", "Motor 1", "Motor 2", "Motor 3", "Back"};
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
      static const char* items[] = {"Distance Control", "Forward", "Backward", "Back"};
      for (int i = 0; i < 4; i++) {
        bool isSelected = (i == menu_index[3]);
        
        if (isSelected) {
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        }
        
        if (i == 1) {
          display.printf("%s Forward [%s] \n", isSelected ? ">" : " ", 
                         motors[selected_motor].fullForward ? "ON" : "OFF");
        } else if (i == 2) {
          display.printf("%s Backward [%s] \n", isSelected ? ">" : " ", 
                         motors[selected_motor].fullBackward ? "ON" : "OFF");
        } else {
          display.printf("%s %s \n", isSelected ? ">" : " ", items[i]);
        }
        
        if (isSelected) {
          display.setTextColor(SSD1306_WHITE);
        }
      }
      break;
    }
      
    case 4: {
      bool isTargetSelected = (menu_index[4] == 0);
      bool isCurrentSelected = (menu_index[4] == 1);
      bool isConfirmSelected = (menu_index[4] == 2);
      bool isBackSelected = (menu_index[4] == 3);
      
      // Target row
      if (isTargetSelected) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.printf("> Target: %s%d mm%s \n", 
                      edit_value ? "[" : "", 
                      motors[selected_motor].target,
                      edit_value ? "]" : "");
        display.setTextColor(SSD1306_WHITE);
      } else {
        display.printf("  Target: %d mm \n", motors[selected_motor].target);
      }

      // Current row
      if (isCurrentSelected) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.printf("> Current: %d mm \n", motors[selected_motor].real_position);
        display.setTextColor(SSD1306_WHITE);
      } else {
        display.printf("  Current: %d mm \n", motors[selected_motor].real_position);
      }

      // Confirm row
      if (isConfirmSelected) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.println("> Confirm");
        display.setTextColor(SSD1306_WHITE);
      } else {
        display.println("  Confirm");
      }

      // Back row
      if (isBackSelected) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.println("> Back");
        display.setTextColor(SSD1306_WHITE);
      } else {
        display.println("  Back");
      }
      break;
    }
      
    case 5: {
      static const char* items[] = {"Cal. Motor 0", "Cal. Motor 1", "Cal. Motor 2", "Cal. Motor 3", "Back"};
      for (int i = 0; i < 5; i++) {
        bool isSelected = (i == menu_index[5]);
        
        if (isSelected) {
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        }
        
        if (i < 4) {
          display.printf("%s %s [%s]\n", isSelected ? ">" : " ", items[i], 
                         motors[i].calibrating ? "ON" : "OFF");
        } else {
          display.printf("%s %s \n", isSelected ? ">" : " ", items[i]);
        }
        
        if (isSelected) {
          display.setTextColor(SSD1306_WHITE);
        }
      }
      break;
    }
      
    case 6: {
      static const char* items[] = {"Servo ON/OFF", "Back"};
      for (int i = 0; i < 2; i++) {
        bool isSelected = (i == menu_index[6]);
        
        if (isSelected) {
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        }
        
        if (i == 0) {
          display.printf("%s %s [%s]\n", isSelected ? ">" : " ", items[i], 
                         servoState ? "ON" : "OFF");
        } else {
          display.printf("%s %s \n", isSelected ? ">" : " ", items[i]);
        }
        
        if (isSelected) {
          display.setTextColor(SSD1306_WHITE);
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
  static uint32_t lastInterruptTime = 0;
  uint32_t interruptTime = millis();
  
  if (interruptTime - lastInterruptTime < 5) return;
  lastInterruptTime = interruptTime;
  
  uint8_t state = (digitalRead(encoderPins[0]) << 1) | digitalRead(encoderPins[1]);
  uint8_t transition = (lastState << 2) | state;

  if (transition == 0b1101 || transition == 0b0100 || transition == 0b0010 || transition == 0b1011) encoder_delta++;
  if (transition == 0b1110 || transition == 0b0111 || transition == 0b0001 || transition == 0b1000) encoder_delta--;

  lastState = state;
}

// ==== Motor Control ====
void startMotor(int motor, int8_t dir) {
  if (motor < 0 || motor > 3) return;
  
  motors[motor].running = true;
  motors[motor].dir = dir;
  motors[motor].move_start_time = millis();
  motors[motor].last_position_update = millis();
  
  digitalWrite(motorPins[motor][0], dir > 0 ? HIGH : LOW);
  digitalWrite(motorPins[motor][1], dir < 0 ? HIGH : LOW);
}

void stopMotor(int motor) {
  if (motor < 0 || motor > 3) return;
  
  motors[motor].running = false;
  motors[motor].fullForward = false;
  motors[motor].fullBackward = false;
  motors[motor].calibrating = false;
  
  digitalWrite(motorPins[motor][0], LOW);
  digitalWrite(motorPins[motor][1], LOW);
  
  sendState();
  if (!updateInProgress) drawMenu();
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
  if (!updateInProgress) drawMenu();
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
  if (!updateInProgress) drawMenu();
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
  if (!updateInProgress) drawMenu();
}

// WebSocket event handler
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch(type) {
    case WS_EVT_CONNECT:
      sendState();
      break;
      
    case WS_EVT_DISCONNECT:
      break;
      
    case WS_EVT_DATA: {
      AwsFrameInfo *info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data);
        
        if (error) return;
        
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
        else if (strcmp(commandType, "check_updates") == 0) {
          String updateInfo = checkForUpdates();
          if (updateInfo != "") {
            JsonDocument responseDoc;
            responseDoc["type"] = "update_info";
            
            // FIXED: Use new ArduinoJson API
            JsonObject dataObj2 = responseDoc["data"].to<JsonObject>();
            
            JsonDocument updateDoc;
            deserializeJson(updateDoc, updateInfo);
            dataObj2["firmware_url"] = updateDoc["firmware_url"].as<String>();
            dataObj2["littlefs_url"] = updateDoc["littlefs_url"].as<String>();
            dataObj2["latest_version"] = updateDoc["latest_version"].as<String>();
            
            String output;
            serializeJson(responseDoc, output);
            ws.textAll(output);
          }
        }
        else if (strcmp(commandType, "update_firmware") == 0) {
          String firmwareUrl = dataObj["url"].as<String>();
          if (firmwareUrl != "" && !updateInProgress) {
            updateStatus = "Starting firmware update...";
            updateInProgress = true;
            currentUpdateType = UPDATE_FIRMWARE;
            updateProgress = 0;
            sendUpdateStatus();
            
            // Create a copy of the URL on the heap
            String *urlCopy = new String(firmwareUrl);
            
            // Start update in background
            xTaskCreate([](void *param) {
              String *url = (String*)param;
              performUpdate(*url, UPDATE_FIRMWARE);
              delete url;
              updateInProgress = false;
              vTaskDelete(NULL);
            }, "Firmware Update", 8192, urlCopy, 1, NULL);
          }
        }
        else if (strcmp(commandType, "update_littlefs") == 0) {
          String littlefsUrl = dataObj["url"].as<String>();
          if (littlefsUrl != "" && !updateInProgress) {
            updateStatus = "Starting LittleFS update...";
            updateInProgress = true;
            currentUpdateType = UPDATE_LITTLEFS;
            updateProgress = 0;
            sendUpdateStatus();
            
            String *urlCopy = new String(littlefsUrl);
            
            xTaskCreate([](void *param) {
              String *url = (String*)param;
              performUpdate(*url, UPDATE_LITTLEFS);
              delete url;
              updateInProgress = false;
              vTaskDelete(NULL);
            }, "LittleFS Update", 8192, urlCopy, 1, NULL);
          }
        }
      }
      break;
    }
      
    case WS_EVT_PING:
    case WS_EVT_PONG:
      // Handle ping/pong if needed
      break;
      
    case WS_EVT_ERROR:
      Serial.printf("WebSocket error\n");
      break;
  }
}

// Send state to all WebSocket clients
void sendState() {
  JsonDocument doc;
  
  // Send data for each motor (0-3)
  for (int i = 0; i < 4; i++) {
    char motorKey[10];
    snprintf(motorKey, sizeof(motorKey), "motor%d", i);
    
    // FIXED: Use new ArduinoJson API
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
    if (motors[i].running) any_running = true;
  }
  doc["globalStatus"] = any_running ? "RUNNING" : "STOPPED";
  
  String output;
  serializeJson(doc, output);
  ws.textAll(output);
}

void setMotorTarget(int motor, int target) {
  if (motor < 0 || motor > 3) return;
  
  motors[motor].target = constrain(target, min_mm, max_mm);
  
  if (motors[motor].target > motors[motor].real_position) {
    startMotor(motor, 1);
  } else if (motors[motor].target < motors[motor].real_position) {
    startMotor(motor, -1);
  } else {
    stopMotor(motor);
  }
  
  if (!updateInProgress) drawMenu();
  sendState();
}

void setupOTA() {
  ArduinoOTA.setHostname("esp32-stanok");
  ArduinoOTA.setPassword("ota123");

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else
        type = "filesystem";

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
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("Update Complete!");
      display.println("Rebooting...");
      display.display();
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("OTA UPDATE");
      display.printf("Progress: %u%%", (progress / (total / 100)));
      display.display();
    })
    .onError([](ota_error_t error) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("OTA ERROR!");
      display.printf("Error: %u", error);
      display.display();
    });

  ArduinoOTA.begin();
}

void handleWebServer() {
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Serve static files
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.serveStatic("/admin", LittleFS, "/admin.html");
  
  // Add cache control for CSS/JS files
  server.on("^\\/(?:style|admin-style|script|admin-script)\\.(?:css|js)$", HTTP_GET, [](AsyncWebServerRequest *request) {
    String path = request->url();
    if (path.endsWith(".css")) {
      request->send(LittleFS, path, "text/css");
    } else if (path.endsWith(".js")) {
      request->send(LittleFS, path, "application/javascript");
    }
  });

  server.begin();
}

void setup() {
  Serial.begin(115200);
  setupI2C();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for (;;);
  }

  // Setup encoder pins and interrupts
  for (int i = 0; i < 3; i++) {
    pinMode(encoderPins[i], INPUT_PULLUP);
  }
  attachInterrupt(digitalPinToInterrupt(encoderPins[0]), readEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(encoderPins[1]), readEncoder, CHANGE);

  // Setup motor pins
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 2; j++) {
      pinMode(motorPins[i][j], OUTPUT);
      digitalWrite(motorPins[i][j], LOW);
    }
  }
  
  // Setup limit switch pins
  for (int i = 0; i < 4; i++) {
    pinMode(limitPins[i], INPUT_PULLUP);
  }
  
  // Initialize LittleFS
  if (!LittleFS.begin()) {
    LittleFS.format();
    LittleFS.begin();
  }

  // Initialize motor states
  for (int i = 0; i < 4; i++) {
    motors[i].real_position = 0;
    motors[i].target = 0;
    motors[i].manual_distance = 0;
    motors[i].running = false;
    motors[i].fullForward = false;
    motors[i].fullBackward = false;
    motors[i].calibrating = false;
  }

  // WiFi setup
  wm.setConfigPortalTimeout(180);
  wm.setHostname("esp32-stanok");
  
  bool res = wm.autoConnect("ESP32-Config", "config123");
  
  if (!res) {
    ESP.restart();
  }
  
  displayStartTime = millis();
  drawIPDisplay();

  setupOTA();
  handleWebServer();

  setServoState(false);
}

void loop() {
  ArduinoOTA.handle();
  
  if (updateInProgress) {
    delay(100);
    return;
  }
  
  if (millis() - displayStartTime < 10000) {
    if (showIP) {
      drawIPDisplay();
    }
    if (millis() - displayStartTime >= 10000 && showIP) {
      showIP = false;
      drawMenu();
    }
  } else {
    // Handle encoder scrolling
    if (encoder_delta != 0 && millis() - lastEncoderUpdate > ENCODER_DEBOUNCE) {
      lastEncoderUpdate = millis();
      
      int8_t delta = (encoder_delta > 0) ? 1 : -1;
      
      if (menu_level == 4 && edit_value) {
        motors[selected_motor].target = constrain(motors[selected_motor].target + delta, min_mm, max_mm);
      } else if (menu_level < 7) {
        menu_index[menu_level] += delta;
        
        static const int8_t max_indices[] = {2, 2, 4, 3, 3, 4, 1};
        menu_index[menu_level] = constrain(menu_index[menu_level], 0, max_indices[menu_level]);
      }
      
      encoder_delta = 0;
      drawMenu();
      sendState();
    }

    // Handle encoder button
    bool btnState = digitalRead(encoderPins[2]);
    if (btnState == LOW && !btnPressed && millis() - lastDebounce > 300) {
      btnPressed = true;
      lastDebounce = millis();

      switch (menu_level) {
        case 0:
          if (menu_index[0] == 0) menu_level = 1;
          else if (menu_index[0] == 1) menu_level = 5;
          else if (menu_index[0] == 2) menu_level = 6;
          break;
          
        case 1:
          if (menu_index[1] == 0) { menu_level = 3; selected_motor = -1; }
          else if (menu_index[1] == 1) menu_level = 2;
          else if (menu_index[1] == 2) menu_level = 0;
          break;
          
        case 2:
          if (menu_index[2] == 4) menu_level = 1;
          else { selected_motor = menu_index[2]; menu_level = 3; }
          break;
          
        case 3:
          selected_action = menu_index[3];
          if (selected_action == 3) {
            menu_level = (selected_motor == -1) ? 1 : 2;
          } 
          else if (selected_action == 0) {
            menu_level = 4;
            edit_value = false;
          } 
          else if (selected_action == 1) {
            (selected_motor == -1) ? toggleAllFullForward() : toggleFullForward(selected_motor);
          }
          else if (selected_action == 2) {
            (selected_motor == -1) ? toggleAllFullBackward() : toggleFullBackward(selected_motor);
          }
          break;
          
        case 4:
          if (menu_index[4] == 0) edit_value = !edit_value;
          else if (menu_index[4] == 2) {
            if (selected_motor == -1) {
              for (int i = 0; i < 4; i++) setMotorTarget(i, motors[i].target);
            } else {
              setMotorTarget(selected_motor, motors[selected_motor].target);
            }
          } 
          else if (menu_index[4] == 3) { menu_level = 3; edit_value = false; }
          break;
          
        case 5:
          if (menu_index[5] == 4) menu_level = 0;
          else toggleCalibration(menu_index[5]);
          break;
          
        case 6:
          if (menu_index[6] == 0) setServoState(!servoState);
          else if (menu_index[6] == 1) menu_level = 0;
          break;
      }
      
      menu_index[menu_level] = 0;
      drawMenu();
      sendState();
    } else if (btnState == HIGH && btnPressed) {
      btnPressed = false;
    }

    // Check limit switches
    for (int i = 0; i < 4; i++) {
      if (motors[i].calibrating && digitalRead(limitPins[i]) == HIGH) {
        stopMotor(i);
        motors[i].manual_distance = 0;
        motors[i].real_position = 0;
        sendState();
        drawMenu();
      }
    }

    // Update motor positions
    uint32_t current_time = millis();
    for (int i = 0; i < 4; i++) {
      if (motors[i].running && !motors[i].fullForward && !motors[i].fullBackward) {
        if (current_time - motors[i].last_position_update >= ms_per_mm) {
          motors[i].last_position_update = current_time;
          
          if (motors[i].dir > 0) {
            motors[i].real_position++;
            motors[i].manual_distance++;
          } else if (motors[i].dir < 0) {
            motors[i].real_position--;
            motors[i].manual_distance--;
          }
          
          // Check if target reached
          if (!motors[i].calibrating) {
            if ((motors[i].dir > 0 && motors[i].manual_distance >= motors[i].target) ||
                (motors[i].dir < 0 && motors[i].manual_distance <= motors[i].target)) {
              stopMotor(i);
            }
          }
          
          sendState();
          if (!updateInProgress) drawMenu();
        }
      }
    }
  }
  
  delay(10);
}