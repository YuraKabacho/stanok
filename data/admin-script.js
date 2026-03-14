document.addEventListener("DOMContentLoaded", function () {
  let websocket;
  let lastUpdateTime = Date.now();
  let uptimeInterval;
  let pendingAction = null;
  let pendingActionData = null;

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
      startUptimeCounter();

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

  // Update interface with data from ESP32
  function updateInterface(data) {
    console.log("Updating admin interface with data:", data);

    // Update IP address
    if (data.ip) {
      document.getElementById("ip-address").textContent = data.ip;
    }

    // Update system status
    if (data.globalStatus) {
      document.getElementById("wifi-status").textContent = data.globalStatus;
    }

    // Update OTA status
    if (data.updateInProgress !== undefined) {
      updateOTAStatus(data);
    }
  }

  // Update OTA status
  function updateOTAStatus(data) {
    const updateProgress = document.getElementById("update-progress");
    const progressBar = document.getElementById("update-progress-bar");
    const progressPercent = document.getElementById("update-percentage");
    const updateStatus = document.getElementById("update-status");
    const startUpdateButton = document.getElementById("start-update-button");
    const checkUpdateButton = document.getElementById("check-update-button");

    if (data.updateInProgress) {
      updateProgress.style.display = "block";
      progressBar.style.width = data.updateProgress + "%";
      progressPercent.textContent = data.updateProgress + "%";
      updateStatus.textContent = data.updateStatus || "Updating...";

      startUpdateButton.disabled = true;
      checkUpdateButton.disabled = true;
    } else {
      if (data.updateStatus) {
        updateStatus.textContent = data.updateStatus;
        if (data.updateProgress === 100) {
          showToast("Update complete! Device will restart.", "success");
          setTimeout(() => {
            updateProgress.style.display = "none";
          }, 3000);
        } else if (
          data.updateStatus.includes("error") ||
          data.updateStatus.includes("failed")
        ) {
          showToast("Update failed: " + data.updateStatus, "error");
        }
      }

      startUpdateButton.disabled = false;
      checkUpdateButton.disabled = false;
    }

    if (data.latestVersion) {
      document.getElementById("latest-version").textContent =
        data.latestVersion;
    }
  }

  // Update connection status
  function updateConnectionStatus(connected) {
    const indicator = document.getElementById("connection-indicator");
    const connectionText = document.getElementById("connection-text");

    if (connected) {
      indicator.classList.add("connected");
      connectionText.textContent = "Connected";
    } else {
      indicator.classList.remove("connected");
      connectionText.textContent = "Disconnected";
    }

    document.getElementById("last-update-time").textContent = "Just now";
  }

  // Start uptime counter
  function startUptimeCounter() {
    let seconds = 0;
    clearInterval(uptimeInterval);

    uptimeInterval = setInterval(() => {
      seconds++;
      const hours = Math.floor(seconds / 3600);
      const minutes = Math.floor((seconds % 3600) / 60);
      const secs = seconds % 60;

      document.getElementById("uptime").textContent = `${hours
        .toString()
        .padStart(2, "0")}:${minutes.toString().padStart(2, "0")}:${secs
        .toString()
        .padStart(2, "0")}`;
    }, 1000);
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

  // Show confirmation modal
  function showConfirmation(title, message, callback, data = null) {
    document.getElementById("modal-title").textContent = title;
    document.getElementById("modal-message").textContent = message;
    document.getElementById("confirmation-modal").classList.add("show");

    pendingAction = callback;
    pendingActionData = data;
  }

  // Hide confirmation modal
  function hideConfirmation() {
    document.getElementById("confirmation-modal").classList.remove("show");
    pendingAction = null;
    pendingActionData = null;
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
        toast.classList.add(type);
      }

      // Show toast
      toast.classList.add("show");

      // Hide after 3 seconds
      setTimeout(() => {
        toast.classList.remove("show");
      }, 3000);
    }
  }

  // Initialize event listeners
  function initializeEventListeners() {
    // Check for updates button
    document
      .getElementById("check-update-button")
      .addEventListener("click", function () {
        this.disabled = true;
        showToast("Checking for updates...", "success");

        setTimeout(() => {
          sendCommand("check_update", {});
          this.disabled = false;
        }, 1000);
      });

    // Start update button
    document
      .getElementById("start-update-button")
      .addEventListener("click", function () {
        showConfirmation(
          "Start OTA Update",
          "Are you sure you want to start the OTA update? The device will restart after update. Do not power off during update!",
          function () {
            sendCommand("check_update", {});
          }
        );
      });

    // Restart button
    document
      .getElementById("restart-button")
      .addEventListener("click", function () {
        showConfirmation(
          "Restart ESP32",
          "Are you sure you want to restart the ESP32? All current operations will be interrupted.",
          function () {
            sendCommand("restart", {});
            showToast("Restarting ESP32...", "success");
          }
        );
      });

    // Reset WiFi button
    document
      .getElementById("reset-wifi-button")
      .addEventListener("click", function () {
        showConfirmation(
          "Reset WiFi Settings",
          "This will reset all WiFi settings and restart in configuration mode. You will need to reconnect to the ESP32's AP to configure WiFi again.",
          function () {
            sendCommand("reset_wifi", {});
            showToast("WiFi settings reset. Reconnecting...", "warning");
          }
        );
      });

    // Format filesystem button
    document
      .getElementById("format-fs-button")
      .addEventListener("click", function () {
        showConfirmation(
          "Format Filesystem",
          "WARNING: This will erase all files from the filesystem including web pages and configuration! This action cannot be undone.",
          function () {
            sendCommand("format_fs", {});
            showToast("Formatting filesystem...", "warning");
          }
        );
      });

    // Calibrate all motors button
    document
      .getElementById("calibrate-all-button")
      .addEventListener("click", function () {
        showConfirmation(
          "Calibrate All Motors",
          "This will start calibration for all motors. Make sure there is enough space for motors to move to limit switches.",
          function () {
            sendCommand("calibrate_all", {});
            showToast("Starting calibration of all motors...", "success");
          }
        );
      });

    // Emergency stop button
    document
      .getElementById("emergency-stop-button")
      .addEventListener("click", function () {
        sendCommand("emergency_stop", {});
        showToast("Emergency stop activated!", "warning");
      });

    // Modal controls
    document
      .getElementById("modal-close")
      .addEventListener("click", hideConfirmation);
    document
      .getElementById("modal-cancel")
      .addEventListener("click", hideConfirmation);
    document
      .getElementById("modal-confirm")
      .addEventListener("click", function () {
        if (pendingAction) {
          pendingAction(pendingActionData);
          hideConfirmation();
        }
      });

    // Close modal on background click
    document
      .getElementById("confirmation-modal")
      .addEventListener("click", function (e) {
        if (e.target === this) {
          hideConfirmation();
        }
      });
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

  // Initialize
  initializeEventListeners();
  connectWebSocket();
});