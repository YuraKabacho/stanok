document.addEventListener("DOMContentLoaded", function () {
  let websocket;
  let allForwardActive = false;
  let allBackwardActive = false;

  // Підключення WebSocket
  function connectWebSocket() {
    const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    const wsUrl = `${protocol}//${window.location.hostname}/ws`;
    websocket = new WebSocket(wsUrl);

    websocket.onopen = function (event) {
      console.log("WebSocket connected");
      showNotification("Підключено до ESP32", "success");
      // Запитуємо IP адресу при підключенні
      sendCommand("get_ip", {});
    };

    websocket.onclose = function (event) {
      console.log("WebSocket disconnected");
      showNotification("Відключено від ESP32", "warning");
      setTimeout(connectWebSocket, 3000);
    };

    websocket.onerror = function (error) {
      console.error("WebSocket error:", error);
    };

    websocket.onmessage = function (event) {
      const data = JSON.parse(event.data);
      updateInterface(data);
    };
  }

  // Ініціалізація слайдерів для кожного двигуна (0-3)
  for (let i = 0; i <= 3; i++) {
    const slider = document.getElementById(`motor${i}-target`);
    const valueDisplay = document.getElementById(`motor${i}-target-value`);

    slider.addEventListener("input", function () {
      valueDisplay.textContent = this.value;
    });

    // Кнопка встановлення цілі
    document
      .getElementById(`motor${i}-set`)
      .addEventListener("click", function () {
        setMotorTarget(i, parseInt(slider.value));
      });

    // Кнопка калібрування
    document
      .getElementById(`motor${i}-calibrate`)
      .addEventListener("click", function () {
        calibrateMotor(i);
      });
  }

  // Кнопки повного вперед і назад для кожного двигуна
  document.querySelectorAll(".motor-full-forward").forEach((button) => {
    button.addEventListener("click", function () {
      const motorId = this.getAttribute("data-motor");
      sendCommand("full_forward", { motor: parseInt(motorId) });
    });
  });

  document.querySelectorAll(".motor-full-backward").forEach((button) => {
    button.addEventListener("click", function () {
      const motorId = this.getAttribute("data-motor");
      sendCommand("full_backward", { motor: parseInt(motorId) });
    });
  });

  // Группове управління
  const allMotorsSlider = document.getElementById("all-motors-target");
  const allMotorsValue = document.getElementById("all-motors-target-value");

  allMotorsSlider.addEventListener("input", function () {
    allMotorsValue.textContent = this.value;
  });

  document
    .getElementById("all-motors-set")
    .addEventListener("click", function () {
      const target = parseInt(allMotorsSlider.value);
      sendCommand("set_all_targets", { target: target });
    });

  document
    .getElementById("all-motors-calibrate")
    .addEventListener("click", function () {
      sendCommand("calibrate_all", {});
    });

  document
    .getElementById("all-full-forward")
    .addEventListener("click", function () {
      sendCommand("all_full_forward", {});
    });

  document
    .getElementById("all-full-backward")
    .addEventListener("click", function () {
      sendCommand("all_full_backward", {});
    });

  document
    .getElementById("emergency-stop")
    .addEventListener("click", function () {
      sendCommand("emergency_stop", {});
    });

  // Керування сервоприводом
  document
    .getElementById("servo-toggle")
    .addEventListener("change", function () {
      sendCommand("set_servo", { state: this.checked });
    });

  // Функції управління
  function setMotorTarget(motorId, target) {
    sendCommand("set_target", { motor: motorId, target: target });
  }

  function calibrateMotor(motorId) {
    sendCommand("calibrate", { motor: motorId });
  }

  function sendCommand(type, data) {
    if (websocket && websocket.readyState === WebSocket.OPEN) {
      const command = {
        type: type,
        data: data,
      };
      websocket.send(JSON.stringify(command));
      console.log("Sent command:", command);
    } else {
      showNotification("Немає з'єднання з ESP32", "warning");
    }
  }

  function updateInterface(data) {
    // Оновлення IP адреси
    if (data.ip) {
      document.getElementById("ip-address").textContent = data.ip;
    }

    // Оновлення значень двигунів (0-3)
    for (let i = 0; i <= 3; i++) {
      const motorData = data[`motor${i}`];
      if (motorData) {
        document.getElementById(`motor${i}-position`).textContent =
          motorData.position;
        document.getElementById(`motor${i}-target-value`).textContent =
          motorData.target;
        document.getElementById(`motor${i}-target`).value = motorData.target;

        // Оновлення статусу
        const statusElement = document.querySelector(
          `#motor${i} .motor-status`
        );
        if (motorData.running) {
          statusElement.textContent = "Активний";
          statusElement.className = "motor-status status-active";
        } else if (motorData.calibrating) {
          statusElement.textContent = "Калібрування";
          statusElement.className = "motor-status status-calibrating";
        } else {
          statusElement.textContent = "Неактивний";
          statusElement.className = "motor-status status-inactive";
        }

        // Оновлення стану кнопок повного ходу
        const forwardBtn = document.querySelector(
          `.motor-full-forward[data-motor="${i}"]`
        );
        const backwardBtn = document.querySelector(
          `.motor-full-backward[data-motor="${i}"]`
        );

        if (motorData.fullForward) {
          forwardBtn.classList.add("btn-active");
        } else {
          forwardBtn.classList.remove("btn-active");
        }

        if (motorData.fullBackward) {
          backwardBtn.classList.add("btn-active");
        } else {
          backwardBtn.classList.remove("btn-active");
        }
      }
    }

    // Оновлення сервоприводу
    if (data.servoState !== undefined) {
      const servoState = data.servoState;
      document.getElementById("servo-state").textContent = servoState
        ? "УВІМКНЕНО"
        : "ВИМКНЕНО";
      document.getElementById("servo-toggle").checked = servoState;

      const statusElement = document.querySelector(
        ".card:nth-last-child(2) .motor-status"
      );
      statusElement.textContent = servoState ? "Активний" : "Вимкнено";
      statusElement.className =
        "motor-status " + (servoState ? "status-active" : "status-inactive");
    }

    // Оновлення глобального статусу
    if (data.globalStatus) {
      document.getElementById("global-status").textContent = data.globalStatus;
    }

    // Оновлення кнопок групового управління
    const allForwardBtn = document.getElementById("all-full-forward");
    const allBackwardBtn = document.getElementById("all-full-backward");

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
      allForwardBtn.classList.add("btn-active");
    } else {
      allForwardBtn.classList.remove("btn-active");
    }

    if (allBackward) {
      allBackwardBtn.classList.add("btn-active");
    } else {
      allBackwardBtn.classList.remove("btn-active");
    }
  }

  function showNotification(message, type) {
    const notification = document.getElementById("notification");
    notification.textContent = message;
    notification.className = "notification";

    switch (type) {
      case "success":
        notification.classList.add("notification-success");
        break;
      case "warning":
        notification.classList.add("notification-warning");
        break;
      case "info":
        notification.classList.add("notification-info");
        break;
    }

    notification.classList.add("show");

    setTimeout(() => {
      notification.classList.remove("show");
    }, 3000);
  }

  // Почати підключення WebSocket
  connectWebSocket();
});
