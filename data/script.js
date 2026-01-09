// Add admin link to main page
function addAdminLink() {
  const mobileHeader = document.querySelector(".mobile-header");
  if (mobileHeader) {
    const adminLink = document.createElement("a");
    adminLink.href = "/admin";
    adminLink.className = "admin-link";
    adminLink.innerHTML = '<i class="fas fa-user-shield"></i>';
    adminLink.style.cssText = `
      position: absolute;
      right: 16px;
      top: 50%;
      transform: translateY(-50%);
      color: white;
      font-size: 1.2rem;
      text-decoration: none;
    `;
    mobileHeader.style.position = "relative";
    mobileHeader.appendChild(adminLink);
  }
}

// Call this in DOMContentLoaded
document.addEventListener("DOMContentLoaded", function () {
  let websocket;
  let lastUpdateTime = Date.now();
  let isSliding = {};

  // Initialize sliding state for each motor
  for (let i = 0; i < 4; i++) {
    isSliding[i] = false;
  }

  // Connect to WebSocket
  function connectWebSocket() {
    const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    const wsUrl = `${protocol}//${window.location.hostname}/ws`;

    console.log(`Connecting to WebSocket: ${wsUrl}`);
    websocket = new WebSocket(wsUrl);

    websocket.onopen = function (event) {
      console.log("WebSocket connected successfully");
      showToast("Connected to ESP32", "success");
      updateConnectionStatus(true);

      // Request initial state
      sendCommand("get_ip", {});
    };

    websocket.onclose = function (event) {
      console.log("WebSocket disconnected", event.code, event.reason);
      showToast("Disconnected from ESP32", "warning");
      updateConnectionStatus(false);

      // Try to reconnect
      setTimeout(connectWebSocket, 3000);
    };

    websocket.onerror = function (error) {
      console.error("WebSocket error:", error);
      updateConnectionStatus(false);
    };

    websocket.onmessage = function (event) {
      try {
        const data = JSON.parse(event.data);
        console.log("Received data:", data);
        lastUpdateTime = Date.now();
        updateInterface(data);
      } catch (error) {
        console.error("Error parsing JSON:", error, "Raw data:", event.data);
      }
    };
  }

  // Update connection status
  function updateConnectionStatus(connected) {
    const statusElement = document.getElementById("connection-status-text");
    const iconElement = document.getElementById("connection-icon");
    const connectionStatus = document.querySelector(".connection-status");

    if (statusElement && iconElement && connectionStatus) {
      if (connected) {
        statusElement.textContent = "Connected";
        iconElement.className = "fas fa-wifi";
        connectionStatus.classList.remove("disconnected");
        connectionStatus.classList.add("connected");
      } else {
        statusElement.textContent = "Disconnected";
        iconElement.className = "fas fa-wifi-slash";
        connectionStatus.classList.remove("connected");
        connectionStatus.classList.add("disconnected");
      }
    }
  }

  // Initialize motor controls
  function initializeMotorControls() {
    // Initialize sliders and buttons for each motor
    for (let i = 0; i < 4; i++) {
      const slider = document.getElementById(`motor${i}-target`);
      const valueDisplay = document.getElementById(`motor${i}-target-value`);
      const displayValue = document.getElementById(`motor${i}-target-display`);

      // Track when user starts sliding
      slider.addEventListener("mousedown", () => {
        isSliding[i] = true;
      });

      slider.addEventListener("touchstart", () => {
        isSliding[i] = true;
      });

      // Update display while sliding (but don't send command)
      slider.addEventListener("input", function () {
        const value = this.value;
        if (valueDisplay) valueDisplay.textContent = value;
        if (displayValue) displayValue.textContent = value;
      });

      // Track when user stops sliding
      slider.addEventListener("mouseup", () => {
        isSliding[i] = false;
      });

      slider.addEventListener("touchend", () => {
        isSliding[i] = false;
      });

      // Individual motor "Set Target" button
      document
        .getElementById(`motor${i}-set-button`)
        .addEventListener("click", function () {
          const target = parseInt(slider.value);
          console.log(`Setting motor ${i} target to ${target} (from button)`);
          setMotorTarget(i, target);
        });

      // Individual motor "Calibrate" button
      document
        .getElementById(`motor${i}-calibrate-button`)
        .addEventListener("click", function () {
          console.log(`Calibrating motor ${i}`);
          calibrateMotor(i);
        });

      // Individual motor "Full Forward" button
      document
        .getElementById(`motor${i}-forward-button`)
        .addEventListener("click", function () {
          console.log(`Full forward for motor ${i}`);
          sendCommand("full_forward", { motor: i });
        });

      // Individual motor "Full Backward" button
      document
        .getElementById(`motor${i}-backward-button`)
        .addEventListener("click", function () {
          console.log(`Full backward for motor ${i}`);
          sendCommand("full_backward", { motor: i });
        });
    }

    // Group controls
    const allMotorsSlider = document.getElementById("all-motors-target");
    const allMotorsValue = document.getElementById("all-motors-target-value");

    // Update display while sliding group slider
    allMotorsSlider.addEventListener("input", function () {
      allMotorsValue.textContent = this.value;
    });

    // Group "Set All" button
    document
      .getElementById("all-motors-set-button")
      .addEventListener("click", function () {
        const target = parseInt(allMotorsSlider.value);
        console.log(`Setting all motors target to ${target}`);
        sendCommand("set_all_targets", { target: target });
      });

    // Group "Calibrate All" button
    document
      .getElementById("all-motors-calibrate-button")
      .addEventListener("click", function () {
        console.log("Calibrating all motors");
        sendCommand("calibrate_all", {});
      });

    // Group "All Forward" button
    document
      .getElementById("all-full-forward-button")
      .addEventListener("click", function () {
        console.log("All motors full forward");
        sendCommand("all_full_forward", {});
      });

    // Group "All Backward" button
    document
      .getElementById("all-full-backward-button")
      .addEventListener("click", function () {
        console.log("All motors full backward");
        sendCommand("all_full_backward", {});
      });

    // Emergency Stop button
    document
      .getElementById("emergency-stop-button")
      .addEventListener("click", function () {
        console.log("Emergency stop");
        sendCommand("emergency_stop", {});
      });

    // Servo control
    document
      .getElementById("servo-toggle")
      .addEventListener("change", function () {
        const state = this.checked;
        console.log(`Setting servo to ${state ? "ON" : "OFF"}`);
        sendCommand("set_servo", { state: state });
      });
  }

  // Send command to ESP32
  function sendCommand(type, data) {
    if (websocket && websocket.readyState === WebSocket.OPEN) {
      const command = {
        type: type,
        data: data,
        timestamp: Date.now(),
      };
      websocket.send(JSON.stringify(command));
      console.log("Sent command:", command);
    } else {
      showToast("No connection to ESP32", "warning");
      console.error(
        "WebSocket is not open. ReadyState:",
        websocket ? websocket.readyState : "no socket"
      );
    }
  }

  // Set motor target
  function setMotorTarget(motorId, target) {
    sendCommand("set_target", { motor: motorId, target: target });
  }

  // Calibrate motor
  function calibrateMotor(motorId) {
    sendCommand("calibrate", { motor: motorId });
  }

  // Update interface with data from ESP32
  function updateInterface(data) {
    console.log("Updating interface with data:", data);

    // Update IP address
    if (data.ip) {
      const ipDesktop = document.getElementById("ip-address");
      const ipMobile = document.getElementById("ip-mobile");
      if (ipDesktop) ipDesktop.textContent = data.ip;
      if (ipMobile) ipMobile.textContent = `IP: ${data.ip}`;
    }

    // Update motor data
    for (let i = 0; i < 4; i++) {
      const motorKey = `motor${i}`;
      const motorData = data[motorKey];

      if (motorData) {
        // Update position display
        const positionElement = document.getElementById(`${motorKey}-position`);
        if (positionElement) {
          positionElement.textContent = motorData.position;
        }

        // Update target display (only if user is not currently sliding)
        const slider = document.getElementById(`${motorKey}-target`);
        const valueDisplay = document.getElementById(
          `${motorKey}-target-value`
        );
        const displayValue = document.getElementById(
          `${motorKey}-target-display`
        );

        if (slider && valueDisplay && displayValue && !isSliding[i]) {
          slider.value = motorData.target;
          valueDisplay.textContent = motorData.target;
          displayValue.textContent = motorData.target;
        }

        // Update status badge
        const statusElement = document.getElementById(`${motorKey}-status`);
        if (statusElement) {
          if (motorData.running) {
            if (motorData.calibrating) {
              statusElement.textContent = "CALIBRATING";
              statusElement.className = "status-badge status-calibrating";
            } else if (motorData.fullForward) {
              statusElement.textContent = "FORWARD";
              statusElement.className = "status-badge status-forward";
            } else if (motorData.fullBackward) {
              statusElement.textContent = "BACKWARD";
              statusElement.className = "status-badge status-backward";
            } else {
              statusElement.textContent = "MOVING";
              statusElement.className = "status-badge status-running";
            }
          } else {
            statusElement.textContent = "IDLE";
            statusElement.className = "status-badge status-idle";
          }
        }

        // Update button states for full forward/backward
        const forwardBtn = document.getElementById(
          `${motorKey}-forward-button`
        );
        const backwardBtn = document.getElementById(
          `${motorKey}-backward-button`
        );

        if (forwardBtn) {
          if (motorData.fullForward) {
            forwardBtn.classList.add("btn-active");
            forwardBtn.innerHTML = '<i class="fas fa-stop"></i> Stop Forward';
          } else {
            forwardBtn.classList.remove("btn-active");
            forwardBtn.innerHTML =
              '<i class="fas fa-forward"></i> Full Forward';
          }
        }

        if (backwardBtn) {
          if (motorData.fullBackward) {
            backwardBtn.classList.add("btn-active");
            backwardBtn.innerHTML = '<i class="fas fa-stop"></i> Stop Backward';
          } else {
            backwardBtn.classList.remove("btn-active");
            backwardBtn.innerHTML =
              '<i class="fas fa-backward"></i> Full Backward';
          }
        }
      }
    }

    // Update servo state
    if (data.servoState !== undefined) {
      const servoState = data.servoState;
      const servoStateText = document.getElementById("servo-state");
      const servoToggle = document.getElementById("servo-toggle");
      const servoStatus = document.getElementById("servo-status");

      if (servoStateText) {
        servoStateText.textContent = servoState ? "ON" : "OFF";
      }

      if (servoToggle) {
        servoToggle.checked = servoState;
      }

      if (servoStatus) {
        servoStatus.textContent = servoState ? "ON" : "OFF";
        servoStatus.className = servoState
          ? "status-badge status-running"
          : "status-badge status-idle";
      }
    }

    // Update global status
    if (data.globalStatus) {
      const globalStatus = document.getElementById("global-status");
      const globalStatusMobile = document.getElementById(
        "global-status-mobile"
      );

      if (globalStatus) {
        globalStatus.textContent = data.globalStatus;
      }

      if (globalStatusMobile) {
        globalStatusMobile.textContent = data.globalStatus;
        globalStatusMobile.className =
          data.globalStatus === "RUNNING"
            ? "status-badge status-running"
            : "status-badge status-idle";
      }
    }

    // Update group control buttons
    const allForwardBtn = document.getElementById("all-full-forward-button");
    const allBackwardBtn = document.getElementById("all-full-backward-button");

    if (allForwardBtn && allBackwardBtn) {
      // Check if all motors are in full forward/backward
      let allForward = true;
      let allBackward = true;

      for (let i = 0; i < 4; i++) {
        const motorKey = `motor${i}`;
        const motorData = data[motorKey];
        if (motorData) {
          if (!motorData.fullForward) allForward = false;
          if (!motorData.fullBackward) allBackward = false;
        }
      }

      if (allForward) {
        allForwardBtn.classList.add("btn-active");
        allForwardBtn.innerHTML =
          '<i class="fas fa-stop"></i> Stop All Forward';
      } else {
        allForwardBtn.classList.remove("btn-active");
        allForwardBtn.innerHTML =
          '<i class="fas fa-fast-forward"></i> All Forward';
      }

      if (allBackward) {
        allBackwardBtn.classList.add("btn-active");
        allBackwardBtn.innerHTML =
          '<i class="fas fa-stop"></i> Stop All Backward';
      } else {
        allBackwardBtn.classList.remove("btn-active");
        allBackwardBtn.innerHTML =
          '<i class="fas fa-fast-backward"></i> All Backward';
      }
    }
  }

  // Show toast notification
  function showToast(message, type) {
    const toast = document.getElementById("toast");
    const toastMessage = document.getElementById("toast-message");

    if (toast && toastMessage) {
      toastMessage.textContent = message;

      // Remove existing type classes
      toast.className = "toast";

      // Add new type class
      if (type) {
        toast.classList.add(`toast-${type}`);
      }

      // Show toast
      toast.classList.add("show");

      // Hide after 3 seconds
      setTimeout(() => {
        toast.classList.remove("show");
      }, 3000);
    }
  }

  // Check connection periodically
  setInterval(() => {
    const now = Date.now();
    const timeSinceLastUpdate = now - lastUpdateTime;

    if (timeSinceLastUpdate > 15000) {
      // 15 seconds without updates
      if (websocket && websocket.readyState === WebSocket.OPEN) {
        console.log("No updates for 15 seconds, sending ping");
        sendCommand("get_ip", {});
      }
    }
  }, 5000);

  // Initialize everything
  initializeMotorControls();
  connectWebSocket();

  // Handle window resize for responsive design
  window.addEventListener("resize", function () {
    // Update any responsive elements if needed
  });
  addAdminLink();
});
