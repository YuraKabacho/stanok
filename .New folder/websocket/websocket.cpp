#include "websocket.h"
#include "../oled/oled.h"

WebSocketHandler wsHandler;

WebSocketHandler::WebSocketHandler() : server(80), ws("/ws") {}

void WebSocketHandler::begin() {
  handleWebServer();
}

void WebSocketHandler::handleWebServer() {
  // Setup WebSocket
  ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, 
                    AwsEventType type, void *arg, uint8_t *data, size_t len) {
    this->onWsEvent(server, client, type, arg, data, len);
  });
  server.addHandler(&ws);

  // Serve static files from LittleFS
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/style.css", "text/css");
  });
  
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/script.js", "application/javascript");
  });

  // Start server
  server.begin();
  Serial.println("HTTP server started");
}

void WebSocketHandler::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                                 AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch(type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      sendState(); // Send initial state to new client
      break;
      
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
      
    case WS_EVT_DATA: {
      AwsFrameInfo *info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        Serial.printf("WebSocket message: %s\n", data);
        
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
          motorCtrl.setMotorTarget(motor, target);
        }
        else if (strcmp(commandType, "calibrate") == 0) {
          int motor = dataObj["motor"];
          motorCtrl.toggleCalibration(motor);
        }
        else if (strcmp(commandType, "set_all_targets") == 0) {
          int target = dataObj["target"];
          for (int i = 0; i < 4; i++) {
            motorCtrl.setMotorTarget(i, target);
          }
        }
        else if (strcmp(commandType, "calibrate_all") == 0) {
          for (int i = 0; i < 4; i++) {
            motorCtrl.toggleCalibration(i);
          }
        }
        else if (strcmp(commandType, "emergency_stop") == 0) {
          motorCtrl.stopAllMotors();
        }
        else if (strcmp(commandType, "set_servo") == 0) {
          bool state = dataObj["state"];
          motorCtrl.setServoState(state);
        }
        else if (strcmp(commandType, "full_forward") == 0) {
          int motor = dataObj["motor"];
          motorCtrl.toggleFullForward(motor);
        }
        else if (strcmp(commandType, "full_backward") == 0) {
          int motor = dataObj["motor"];
          motorCtrl.toggleFullBackward(motor);
        }
        else if (strcmp(commandType, "all_full_forward") == 0) {
          motorCtrl.toggleAllFullForward();
        }
        else if (strcmp(commandType, "all_full_backward") == 0) {
          motorCtrl.toggleAllFullBackward();
        }
        else if (strcmp(commandType, "get_ip") == 0) {
          sendState(); // Will include IP address
        }
      }
      break;
    }
      
    case WS_EVT_ERROR:
      Serial.printf("WebSocket error\n");
      break;
  }
}

void WebSocketHandler::sendState() {
  JsonDocument doc;
  
  // Send data for each motor (0-3)
  for (int i = 0; i < 4; i++) {
    char motorKey[10];
    sprintf(motorKey, "motor%d", i);
    
    JsonObject motorData = doc[motorKey].to<JsonObject>();
    motorData["position"] = motorCtrl.motors[i].real_position;
    motorData["target"] = motorCtrl.motors[i].target;
    motorData["running"] = motorCtrl.motors[i].running;
    motorData["calibrating"] = motorCtrl.motors[i].calibrating;
    motorData["fullForward"] = motorCtrl.motors[i].fullForward;
    motorData["fullBackward"] = motorCtrl.motors[i].fullBackward;
  }
  
  doc["servoState"] = motorCtrl.servoState;
  doc["ip"] = WiFi.localIP().toString();
  
  bool any_running = false;
  for (int i = 0; i < 4; i++) {
    if (motorCtrl.motors[i].running) {
      any_running = true;
      break;
    }
  }
  doc["globalStatus"] = any_running ? "RUNNING" : "STOPPED";
  
  String output;
  serializeJson(doc, output);
  ws.textAll(output);
}