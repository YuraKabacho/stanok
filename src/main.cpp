#include <Arduino.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

// ==== OLED ====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==== Encoder pins ====
#define ENCODER_A 32
#define ENCODER_B 33
#define ENCODER_BTN 25

// ==== L298N pins ====
#define IN1_M1 13
#define IN2_M1 12
#define IN1_M2 27
#define IN2_M2 26
#define IN1_M3 14
#define IN2_M3 4
#define IN1_M4 2
#define IN2_M4 15

// ==== Limit Switches ====
#define LIMIT_SWITCH_M1 5
#define LIMIT_SWITCH_M2 23
#define LIMIT_SWITCH_M3 35
#define LIMIT_SWITCH_M4 34

// ==== Servos ====
Servo myServo1;
Servo myServo2;
int servoPin1 = 18;
int servoPin2 = 19;
bool servoState = false;

// ==== Wi-Fi Access Point credentials ====
const char* ssid = "ESP32_Motor_Control";
const char* password = "12345678";

// ==== Web Server ====
AsyncWebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// ==== Parameters ====
const int max_mm = 20;
const int min_mm = 0;
const int ms_per_mm = 6800;

// ==== Encoder debounce ====
#define ENCODER_DEBOUNCE 40
unsigned long lastEncoderUpdate = 0;

// ==== Variables ====
volatile int8_t encoder_delta = 0;
unsigned long lastDebounce = 0;
bool btnPressed = false;

// Motor variables
struct Motor {
  int manual_distance;
  int real_position;
  int target;
  bool running;
  unsigned long move_start_time;
  int dir;
  bool fullForward;
  bool fullBackward;
  bool calibrating;
  int limitSwitchPin;
};

// Правильний порядок двигунів: 0, 1, 2, 3
Motor motors[4] = {
  {0, 0, 0, false, 0, 0, false, false, false, LIMIT_SWITCH_M1}, // Двигун 0
  {0, 0, 0, false, 0, 0, false, false, false, LIMIT_SWITCH_M2}, // Двигун 1
  {0, 0, 0, false, 0, 0, false, false, false, LIMIT_SWITCH_M3}, // Двигун 2
  {0, 0, 0, false, 0, 0, false, false, false, LIMIT_SWITCH_M4}  // Двигун 3
};

// Menu variables
int menu_level = 0;
int menu_index[7] = {0};
int selected_motor = 0;
int selected_action = 0;
bool edit_value = false;

// HTML страница как строка
const char* html_content = R"rawliteral(
<!DOCTYPE html>
<html lang="uk">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Керування двигунами ESP32</title>
    <style>
        :root {
            --primary: #2c3e50;
            --secondary: #3498db;
            --accent: #e74c3c;
            --success: #2ecc71;
            --warning: #f39c12;
            --light: #ecf0f1;
            --dark: #34495e;
            --text: #2c3e50;
            --bg: #f5f7fa;
        }
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        }
        
        body {
            background-color: var(--bg);
            color: var(--text);
            padding: 20px;
            min-height: 100vh;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
        }
        
        header {
            background: linear-gradient(135deg, var(--primary), var(--secondary));
            color: white;
            padding: 20px;
            border-radius: 10px;
            margin-bottom: 20px;
            box-shadow: 0 4px 15px rgba(0, 0, 0, 0.1);
            text-align: center;
        }
        
        h1 {
            margin-bottom: 10px;
            font-size: 2.5rem;
        }
        
        .status-bar {
            display: flex;
            justify-content: space-between;
            background-color: var(--dark);
            color: white;
            padding: 10px 15px;
            border-radius: 5px;
            margin-top: 10px;
            font-size: 0.9rem;
        }
        
        .dashboard {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 20px;
            margin-bottom: 20px;
        }
        
        .card {
            background: white;
            border-radius: 10px;
            padding: 20px;
            box-shadow: 0 4px 15px rgba(0, 0, 0, 0.05);
            transition: transform 0.3s ease;
        }
        
        .card:hover {
            transform: translateY(-5px);
        }
        
        .card-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 15px;
            padding-bottom: 10px;
            border-bottom: 1px solid #eee;
        }
        
        .card-title {
            font-size: 1.2rem;
            font-weight: 600;
            color: var(--primary);
        }
        
        .motor-status {
            display: inline-block;
            padding: 3px 8px;
            border-radius: 20px;
            font-size: 0.8rem;
            font-weight: 500;
        }
        
        .status-active {
            background-color: rgba(46, 204, 113, 0.2);
            color: var(--success);
        }
        
        .status-inactive {
            background-color: rgba(236, 240, 241, 0.8);
            color: var(--dark);
        }
        
        .status-calibrating {
            background-color: rgba(243, 156, 18, 0.2);
            color: var(--warning);
        }
        
        .control-group {
            margin-bottom: 15px;
        }
        
        .control-label {
            display: block;
            margin-bottom: 5px;
            font-weight: 500;
        }
        
        .value-display {
            font-size: 1.5rem;
            font-weight: 700;
            color: var(--secondary);
            text-align: center;
            margin: 10px 0;
        }
        
        .slider-container {
            display: flex;
            align-items: center;
            gap: 10px;
        }
        
        .slider {
            flex: 1;
            -webkit-appearance: none;
            height: 8px;
            border-radius: 4px;
            background: #ddd;
            outline: none;
        }
        
        .slider::-webkit-slider-thumb {
            -webkit-appearance: none;
            width: 20px;
            height: 20px;
            border-radius: 50%;
            background: var(--secondary);
            cursor: pointer;
            transition: background 0.3s;
        }
        
        .slider::-webkit-slider-thumb:hover {
            background: var(--primary);
        }
        
        .btn {
            padding: 10px 15px;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            font-weight: 500;
            transition: all 0.3s ease;
        }
        
        .btn-primary {
            background-color: var(--secondary);
            color: white;
        }
        
        .btn-primary:hover {
            background-color: var(--primary);
        }
        
        .btn-success {
            background-color: var(--success);
            color: white;
        }
        
        .btn-success:hover {
            background-color: #27ae60;
        }
        
        .btn-warning {
            background-color: var(--warning);
            color: white;
        }
        
        .btn-warning:hover {
            background-color: #e67e22;
        }
        
        .btn-danger {
            background-color: var(--accent);
            color: white;
        }
        
        .btn-danger:hover {
            background-color: #c0392b;
        }
        
        .btn-active {
            background-color: var(--primary);
            color: white;
            box-shadow: 0 0 10px rgba(0,0,0,0.2);
        }
        
        .btn-group {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 10px;
            margin-top: 15px;
        }
        
        .servo-control {
            text-align: center;
            margin-top: 20px;
        }
        
        .switch {
            position: relative;
            display: inline-block;
            width: 60px;
            height: 30px;
        }
        
        .switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }
        
        .slider-switch {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: #ccc;
            transition: .4s;
            border-radius: 30px;
        }
        
        .slider-switch:before {
            position: absolute;
            content: "";
            height: 22px;
            width: 22px;
            left: 4px;
            bottom: 4px;
            background-color: white;
            transition: .4s;
            border-radius: 50%;
        }
        
        input:checked + .slider-switch {
            background-color: var(--success);
        }
        
        input:checked + .slider-switch:before {
            transform: translateX(30px);
        }
        
        .notification {
            position: fixed;
            bottom: 20px;
            right: 20px;
            padding: 15px 20px;
            border-radius: 5px;
            color: white;
            opacity: 0;
            transform: translateY(20px);
            transition: all 0.3s ease;
            z-index: 1000;
            box-shadow: 0 4px 15px rgba(0, 0, 0, 0.1);
        }
        
        .notification.show {
            opacity: 1;
            transform: translateY(0);
        }
        
        .notification-success {
            background-color: var(--success);
        }
        
        .notification-warning {
            background-color: var(--warning);
        }
        
        .notification-info {
            background-color: var(--secondary);
        }
        
        @media (max-width: 768px) {
            .dashboard {
                grid-template-columns: 1fr;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>Керування двигунами ESP32</h1>
            <p>Інтерфейс для контролю 4 двигунів та сервоприводу</p>
            <div class="status-bar">
                <span>Статус: <span id="global-status">ПІДКЛЮЧЕНО</span></span>
            </div>
        </header>
        
        <div class="dashboard">
            <!-- Двигун 1 -->
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Двигун 1 (Піни: 13,12)</span>
                    <span class="motor-status status-inactive">Неактивний</span>
                </div>
                <div class="control-group">
                    <label class="control-label">Поточна позиція (мм)</label>
                    <div class="value-display" id="motor0-position">0</div>
                </div>
                <div class="control-group">
                    <label class="control-label">Цільова позиція (мм)</label>
                    <div class="slider-container">
                        <input type="range" min="0" max="20" value="0" class="slider" id="motor0-target">
                        <span id="motor0-target-value">0</span>
                    </div>
                </div>
                <div class="btn-group">
                    <button class="btn btn-primary" id="motor0-set">Встановити</button>
                    <button class="btn btn-success" id="motor0-calibrate">Калібрувати</button>
                </div>
                <div class="btn-group">
                    <button class="btn btn-warning motor-full-forward" data-motor="0">Повний вперед</button>
                    <button class="btn btn-warning motor-full-backward" data-motor="0">Повний назад</button>
                </div>
            </div>
            
            <!-- Двигун 2 -->
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Двигун 2 (Піни: 27,26)</span>
                    <span class="motor-status status-inactive">Неактивний</span>
                </div>
                <div class="control-group">
                    <label class="control-label">Поточна позиція (мм)</label>
                    <div class="value-display" id="motor1-position">0</div>
                </div>
                <div class="control-group">
                    <label class="control-label">Цільова позиція (мм)</label>
                    <div class="slider-container">
                        <input type="range" min="0" max="20" value="0" class="slider" id="motor1-target">
                        <span id="motor1-target-value">0</span>
                    </div>
                </div>
                <div class="btn-group">
                    <button class="btn btn-primary" id="motor1-set">Встановити</button>
                    <button class="btn btn-success" id="motor1-calibrate">Калібрувати</button>
                </div>
                <div class="btn-group">
                    <button class="btn btn-warning motor-full-forward" data-motor="1">Повний вперед</button>
                    <button class="btn btn-warning motor-full-backward" data-motor="1">Повний назад</button>
                </div>
            </div>
            
            <!-- Двигун 3 -->
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Двигун 3 (Піни: 14,16)</span>
                    <span class="motor-status status-inactive">Неактивний</span>
                </div>
                <div class="control-group">
                    <label class="control-label">Поточна позиція (мм)</label>
                    <div class="value-display" id="motor2-position">0</div>
                </div>
                <div class="control-group">
                    <label class="control-label">Цільова позиція (мм)</label>
                    <div class="slider-container">
                        <input type="range" min="0" max="20" value="0" class="slider" id="motor2-target">
                        <span id="motor2-target-value">0</span>
                    </div>
                </div>
                <div class="btn-group">
                    <button class="btn btn-primary" id="motor2-set">Встановити</button>
                    <button class="btn btn-success" id="motor2-calibrate">Калібрувати</button>
                </div>
                <div class="btn-group">
                    <button class="btn btn-warning motor-full-forward" data-motor="2">Повний вперед</button>
                    <button class="btn btn-warning motor-full-backward" data-motor="2">Повний назад</button>
                </div>
            </div>
            
            <!-- Двигун 4 -->
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Двигун 4 (Піни: 2,15)</span>
                    <span class="motor-status status-inactive">Неактивний</span>
                </div>
                <div class="control-group">
                    <label class="control-label">Поточна позиція (мм)</label>
                    <div class="value-display" id="motor3-position">0</div>
                </div>
                <div class="control-group">
                    <label class="control-label">Цільова позиція (мм)</label>
                    <div class="slider-container">
                        <input type="range" min="0" max="20" value="0" class="slider" id="motor3-target">
                        <span id="motor3-target-value">0</span>
                    </div>
                </div>
                <div class="btn-group">
                    <button class="btn btn-primary" id="motor3-set">Встановити</button>
                    <button class="btn btn-success" id="motor3-calibrate">Калібрувати</button>
                </div>
                <div class="btn-group">
                    <button class="btn btn-warning motor-full-forward" data-motor="3">Повний вперед</button>
                    <button class="btn btn-warning motor-full-backward" data-motor="3">Повний назад</button>
                </div>
            </div>
            
            <!-- Сервопривід -->
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Сервопривід (Піни: 18,19)</span>
                    <span class="motor-status status-inactive">Вимкнено</span>
                </div>
                <div class="servo-control">
                    <label class="switch">
                        <input type="checkbox" id="servo-toggle">
                        <span class="slider-switch"></span>
                    </label>
                    <p style="margin-top: 10px;">Стан: <span id="servo-state">ВИМКНЕНО</span></p>
                </div>
            </div>
            
            <!-- Группове управління -->
            <div class="card">
                <div class="card-header">
                    <span class="card-title">Группове управління</span>
                </div>
                <div class="control-group">
                    <label class="control-label">Цільова позиція для всіх двигунів (мм)</label>
                    <div class="slider-container">
                        <input type="range" min="0" max="20" value="0" class="slider" id="all-motors-target">
                        <span id="all-motors-target-value">0</span>
                    </div>
                </div>
                <div class="btn-group" style="grid-template-columns: 1fr;">
                    <button class="btn btn-primary" id="all-motors-set">Встановити для всіх</button>
                    <button class="btn btn-success" id="all-motors-calibrate">Калібрувати всі</button>
                    <button class="btn btn-warning" id="all-full-forward">Всі повний вперед</button>
                    <button class="btn btn-warning" id="all-full-backward">Всі повний назад</button>
                    <button class="btn btn-danger" id="emergency-stop">Аварійна зупинка</button>
                </div>
            </div>
        </div>
    </div>
    
    <div class="notification" id="notification">
        Повідомлення
    </div>

    <script>
        document.addEventListener('DOMContentLoaded', function() {
            let websocket;
            let allForwardActive = false;
            let allBackwardActive = false;
            
            // Підключення WebSocket
            function connectWebSocket() {
                const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
                const wsUrl = `${protocol}//${window.location.hostname}:81`;
                websocket = new WebSocket(wsUrl);
                
                websocket.onopen = function(event) {
                    console.log('WebSocket connected');
                    showNotification('Підключено до ESP32', 'success');
                };
                
                websocket.onclose = function(event) {
                    console.log('WebSocket disconnected');
                    showNotification('Відключено від ESP32', 'warning');
                    setTimeout(connectWebSocket, 3000);
                };
                
                websocket.onerror = function(error) {
                    console.error('WebSocket error:', error);
                };
                
                websocket.onmessage = function(event) {
                    const data = JSON.parse(event.data);
                    updateInterface(data);
                };
            }
            
            // Ініціалізація слайдерів для кожного двигуна (0-3)
            for (let i = 0; i <= 3; i++) {
                const slider = document.getElementById(`motor${i}-target`);
                const valueDisplay = document.getElementById(`motor${i}-target-value`);
                
                slider.addEventListener('input', function() {
                    valueDisplay.textContent = this.value;
                });
                
                // Кнопка встановлення цілі
                document.getElementById(`motor${i}-set`).addEventListener('click', function() {
                    setMotorTarget(i, parseInt(slider.value));
                });
                
                // Кнопка калібрування
                document.getElementById(`motor${i}-calibrate`).addEventListener('click', function() {
                    calibrateMotor(i);
                });
            }
            
            // Кнопки повного вперед і назад для кожного двигуна
            document.querySelectorAll('.motor-full-forward').forEach(button => {
                button.addEventListener('click', function() {
                    const motorId = this.getAttribute('data-motor');
                    sendCommand('full_forward', { motor: parseInt(motorId) });
                });
            });
            
            document.querySelectorAll('.motor-full-backward').forEach(button => {
                button.addEventListener('click', function() {
                    const motorId = this.getAttribute('data-motor');
                    sendCommand('full_backward', { motor: parseInt(motorId) });
                });
            });
            
            // Группове управління
            const allMotorsSlider = document.getElementById('all-motors-target');
            const allMotorsValue = document.getElementById('all-motors-target-value');
            
            allMotorsSlider.addEventListener('input', function() {
                allMotorsValue.textContent = this.value;
            });
            
            document.getElementById('all-motors-set').addEventListener('click', function() {
                const target = parseInt(allMotorsSlider.value);
                sendCommand('set_all_targets', { target: target });
            });
            
            document.getElementById('all-motors-calibrate').addEventListener('click', function() {
                sendCommand('calibrate_all', {});
            });
            
            document.getElementById('all-full-forward').addEventListener('click', function() {
                sendCommand('all_full_forward', {});
            });
            
            document.getElementById('all-full-backward').addEventListener('click', function() {
                sendCommand('all_full_backward', {});
            });
            
            document.getElementById('emergency-stop').addEventListener('click', function() {
                sendCommand('emergency_stop', {});
            });
            
            // Керування сервоприводом
            document.getElementById('servo-toggle').addEventListener('change', function() {
                sendCommand('set_servo', { state: this.checked });
            });
            
            // Функції управління
            function setMotorTarget(motorId, target) {
                sendCommand('set_target', { motor: motorId, target: target });
            }
            
            function calibrateMotor(motorId) {
                sendCommand('calibrate', { motor: motorId });
            }
            
            function sendCommand(type, data) {
                if (websocket && websocket.readyState === WebSocket.OPEN) {
                    const command = {
                        type: type,
                        data: data
                    };
                    websocket.send(JSON.stringify(command));
                    console.log('Sent command:', command);
                } else {
                    showNotification('Немає з\'єднання з ESP32', 'warning');
                }
            }
            
            function updateInterface(data) {
                // Оновлення значень двигунів (0-3)
                for (let i = 0; i <= 3; i++) {
                    const motorData = data[`motor${i}`];
                    if (motorData) {
                        document.getElementById(`motor${i}-position`).textContent = motorData.position;
                        document.getElementById(`motor${i}-target-value`).textContent = motorData.target;
                        document.getElementById(`motor${i}-target`).value = motorData.target;
                        
                        // Оновлення статусу
                        const statusElement = document.querySelector(`#motor${i} .motor-status`);
                        if (motorData.running) {
                            statusElement.textContent = 'Активний';
                            statusElement.className = 'motor-status status-active';
                        } else if (motorData.calibrating) {
                            statusElement.textContent = 'Калібрування';
                            statusElement.className = 'motor-status status-calibrating';
                        } else {
                            statusElement.textContent = 'Неактивний';
                            statusElement.className = 'motor-status status-inactive';
                        }
                        
                        // Оновлення стану кнопок повного ходу
                        const forwardBtn = document.querySelector(`.motor-full-forward[data-motor="${i}"]`);
                        const backwardBtn = document.querySelector(`.motor-full-backward[data-motor="${i}"]`);
                        
                        if (motorData.fullForward) {
                            forwardBtn.classList.add('btn-active');
                        } else {
                            forwardBtn.classList.remove('btn-active');
                        }
                        
                        if (motorData.fullBackward) {
                            backwardBtn.classList.add('btn-active');
                        } else {
                            backwardBtn.classList.remove('btn-active');
                        }
                    }
                }
                
                // Оновлення сервоприводу
                if (data.servoState !== undefined) {
                    const servoState = data.servoState;
                    document.getElementById('servo-state').textContent = servoState ? 'УВІМКНЕНО' : 'ВИМКНЕНО';
                    document.getElementById('servo-toggle').checked = servoState;
                    
                    const statusElement = document.querySelector('.card:nth-last-child(2) .motor-status');
                    statusElement.textContent = servoState ? 'Активний' : 'Вимкнено';
                    statusElement.className = 'motor-status ' + (servoState ? 'status-active' : 'status-inactive');
                }
                
                // Оновлення глобального статусу
                if (data.globalStatus) {
                    document.getElementById('global-status').textContent = data.globalStatus;
                }
                
                // Оновлення кнопок групового управління
                const allForwardBtn = document.getElementById('all-full-forward');
                const allBackwardBtn = document.getElementById('all-full-backward');
                
                // Перевірка стану всіх двигунів для групових кнопок
                let allForward = true;
                let allBackward = true;
                
                for (let i = 0; i <= 3; i++) {
                    const motorData = data[`motor${i}`];
                    if (motorData) {
                        if (!motorData.fullForward) allForward = false;
                        if (!motorData.fullBackward) allBackward = false;
                    }
                }
                
                if (allForward) {
                    allForwardBtn.classList.add('btn-active');
                } else {
                    allForwardBtn.classList.remove('btn-active');
                }
                
                if (allBackward) {
                    allBackwardBtn.classList.add('btn-active');
                } else {
                    allBackwardBtn.classList.remove('btn-active');
                }
            }
            
            function showNotification(message, type) {
                const notification = document.getElementById('notification');
                notification.textContent = message;
                notification.className = 'notification';
                
                switch (type) {
                    case 'success':
                        notification.classList.add('notification-success');
                        break;
                    case 'warning':
                        notification.classList.add('notification-warning');
                        break;
                    case 'info':
                        notification.classList.add('notification-info');
                        break;
                }
                
                notification.classList.add('show');
                
                setTimeout(() => {
                    notification.classList.remove('show');
                }, 3000);
            }
            
            // Почати підключення WebSocket
            connectWebSocket();
        });
    </script>
</body>
</html>
)rawliteral";

// Function prototypes
void setServoState(bool state);
void setMotorTarget(int motor, int target);
void drawMenu();
void sendState();
void toggleCalibration(int motor);
void startMotor(int motor, int dir);
void stopMotor(int motor);
void stopAllMotors();
void toggleFullForward(int motor);
void toggleFullBackward(int motor);
void toggleAllFullForward();
void toggleAllFullBackward();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
void IRAM_ATTR readEncoder();
void setupI2C();

// ==== I2C ====
void setupI2C() {
  Wire.begin(21, 22);
  Wire.setClock(400000);
}

// ==== Servo Control Function ====
void setServoState(bool state) {
  servoState = state;
  if (servoState) {
    if (!myServo1.attached()) {
      myServo1.attach(servoPin1, 500, 2400);
    }
    if (!myServo2.attached()) {
      myServo2.attach(servoPin2, 500, 2400);
    }
    myServo1.write(180);
    myServo2.write(180);
  } else {
    myServo1.detach();
    myServo2.detach();
  }
  sendState();
}

// ==== OLED ====
void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Display header
  display.drawRect(0, 0, SCREEN_WIDTH, 14, SSD1306_WHITE);
  display.setCursor(4, 4);
  
  if (menu_level == 0) {
    display.print("MAIN MENU");
  }
  else if (menu_level == 1) {
    display.print("MOTOR CONTROL TYPE");
  }
  else if (menu_level == 2) {
    display.print("MOTOR SELECT");
  }
  else if (menu_level == 3) {
    display.print("ACTION SELECT");
  }
  else if (menu_level == 4) {
    display.print("DISTANCE CONTROL");
  }
  else if (menu_level == 5) {
    display.print("CALIBRATION");
  }
  else if (menu_level == 6) {
    display.print("SERVO CONTROL");
  }

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
  
  uint8_t state = (digitalRead(ENCODER_A) << 1) | digitalRead(ENCODER_B);
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
  
  switch(motor) {
    case 0: // Двигун 0
      digitalWrite(IN1_M1, dir > 0 ? HIGH : LOW);
      digitalWrite(IN2_M1, dir < 0 ? HIGH : LOW);
      break;
    case 1: // Двигун 1
      digitalWrite(IN1_M2, dir > 0 ? HIGH : LOW);
      digitalWrite(IN2_M2, dir < 0 ? HIGH : LOW);
      break;
    case 2: // Двигун 2
      digitalWrite(IN1_M3, dir > 0 ? HIGH : LOW);
      digitalWrite(IN2_M3, dir < 0 ? HIGH : LOW);
      break;
    case 3: // Двигун 3
      digitalWrite(IN1_M4, dir > 0 ? HIGH : LOW);
      digitalWrite(IN2_M4, dir < 0 ? HIGH : LOW);
      break;
  }
}

void stopMotor(int motor) {
  if (motor < 0 || motor > 3) return;
  
  motors[motor].running = false;
  motors[motor].fullForward = false;
  motors[motor].fullBackward = false;
  motors[motor].calibrating = false;
  
  switch(motor) {
    case 0: // Двигун 0
      digitalWrite(IN1_M1, LOW);
      digitalWrite(IN2_M1, LOW);
      break;
    case 1: // Двигун 1
      digitalWrite(IN1_M2, LOW);
      digitalWrite(IN2_M2, LOW);
      break;
    case 2: // Двигун 2
      digitalWrite(IN1_M3, LOW);
      digitalWrite(IN2_M3, LOW);
      break;
    case 3: // Двигун 3
      digitalWrite(IN1_M4, LOW);
      digitalWrite(IN2_M4, LOW);
      break;
  }
  
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
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      }
      break;
    case WStype_TEXT: {
        Serial.printf("[%u] get Text: %s\n", num, payload);
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
          Serial.print(F("deserializeJson() failed: "));
          Serial.println(error.f_str());
          return;
        }
        
        const char* commandType = doc["type"];
        JsonObject data = doc["data"];
        
        if (strcmp(commandType, "set_target") == 0) {
          int motor = data["motor"];
          int target = data["target"];
          setMotorTarget(motor, target);
        }
        else if (strcmp(commandType, "calibrate") == 0) {
          int motor = data["motor"];
          toggleCalibration(motor);
        }
        else if (strcmp(commandType, "set_all_targets") == 0) {
          int target = data["target"];
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
          bool state = data["state"];
          setServoState(state);
        }
        else if (strcmp(commandType, "full_forward") == 0) {
          int motor = data["motor"];
          toggleFullForward(motor);
        }
        else if (strcmp(commandType, "full_backward") == 0) {
          int motor = data["motor"];
          toggleFullBackward(motor);
        }
        else if (strcmp(commandType, "all_full_forward") == 0) {
          toggleAllFullForward();
        }
        else if (strcmp(commandType, "all_full_backward") == 0) {
          toggleAllFullBackward();
        }
      }
      break;
  }
}

// Виправлена функція для відправки стану через WebSocket
void sendState() {
  JsonDocument doc;
  
  // Відправляємо дані для кожного двигуна (0-3)
  for (int i = 0; i < 4; i++) {
    char motorKey[10];
    sprintf(motorKey, "motor%d", i); // motor0, motor1, motor2, motor3
    
    JsonObject motorData = doc[motorKey].to<JsonObject>();
    motorData["position"] = motors[i].real_position;  // Використовуємо real_position
    motorData["target"] = motors[i].target;
    motorData["running"] = motors[i].running;
    motorData["calibrating"] = motors[i].calibrating;
    motorData["fullForward"] = motors[i].fullForward;
    motorData["fullBackward"] = motors[i].fullBackward;
  }
  
  doc["servoState"] = servoState;
  
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
  webSocket.broadcastTXT(output);
}

void setMotorTarget(int motor, int target) {
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
  
  drawMenu();
  sendState();
}

void setup() {
  Serial.begin(115200);
  setupI2C();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
    for (;;);
  }

  pinMode(ENCODER_A, INPUT_PULLUP);
  pinMode(ENCODER_B, INPUT_PULLUP);
  pinMode(ENCODER_BTN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_A), readEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_B), readEncoder, CHANGE);

  // Ініціалізація пінів двигунів
  pinMode(IN1_M1, OUTPUT);
  pinMode(IN2_M1, OUTPUT);
  pinMode(IN1_M2, OUTPUT);
  pinMode(IN2_M2, OUTPUT);
  pinMode(IN1_M3, OUTPUT);
  pinMode(IN2_M3, OUTPUT);
  pinMode(IN1_M4, OUTPUT);
  pinMode(IN2_M4, OUTPUT);
  
  // Ініціалізація кінцевиків
  pinMode(LIMIT_SWITCH_M1, INPUT_PULLUP);
  pinMode(LIMIT_SWITCH_M2, INPUT_PULLUP);
  pinMode(LIMIT_SWITCH_M3, INPUT_PULLUP);
  pinMode(LIMIT_SWITCH_M4, INPUT_PULLUP);
  
  // Гарантоване вимикання всіх двигунів при старті
  stopAllMotors();
  
  // Додаткова затримка для стабілізації
  delay(100);
  
  setServoState(false);

  // Ініціалізація SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
    return;
  }

  // Створення точки доступу
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // Налаштування веб-сервера
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", html_content);
  });

  // Запуск сервера
  server.begin();
  
  // Запуск WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  drawMenu();
}

void loop() {
  webSocket.loop();
  
  // Handle scrolling
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
        
        switch (menu_level) {
          case 0:
            menu_index[0] = constrain(menu_index[0], 0, 2);
            break;
          case 1:
            menu_index[1] = constrain(menu_index[1], 0, 2);
            break;
          case 2:
            menu_index[2] = constrain(menu_index[2], 0, 4);
            break;
          case 3:
            menu_index[3] = constrain(menu_index[3], 0, 3);
            break;
          case 4:
            menu_index[4] = constrain(menu_index[4], 0, 3);
            break;
          case 5:
            menu_index[5] = constrain(menu_index[5], 0, 4);
            break;
          case 6:
            menu_index[6] = constrain(menu_index[6], 0, 1);
            break;
        }
      }
      
      encoder_delta = 0;
      drawMenu();
      sendState();
    }
  }

  bool btnState = digitalRead(ENCODER_BTN);
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
          if (selected_motor == -1) {
            menu_level = 1;
            } else {
            menu_level = 2;
          }
        } 
        else if (selected_action == 0) {
          if (selected_motor == -1) {
            menu_level = 4;
            menu_index[4] = 0;
            edit_value = false;
          } else {
            menu_level = 4;
            menu_index[4] = 0;
            edit_value = false;
          }
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

  // Перевірка кінцевиків для кожного двигуна окремо
  for (int i = 0; i < 4; i++) {
    if (motors[i].calibrating && digitalRead(motors[i].limitSwitchPin) == HIGH) {
      stopMotor(i);
      motors[i].manual_distance = 0;
      motors[i].real_position = 0;
      sendState();
      drawMenu();
    }
  }

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
        
        // Відправляємо оновлення стану після кожної зміни позиції
        sendState();
        drawMenu();
      }
    }
  }
  
  delay(10);
}