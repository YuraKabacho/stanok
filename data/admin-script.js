document.addEventListener("DOMContentLoaded", function () {
  let websocket;
  let lastUpdateTime = Date.now();
  let pendingAction = null;
  let pendingActionData = null;

  // Connect to WebSocket
  function connectWebSocket() {
    const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    const wsUrl = `${protocol}//${window.location.hostname}/ws`;

    websocket = new WebSocket(wsUrl);

    websocket.onopen = function (event) {
      showToast("Connected to ESP32", "success");
      updateConnectionStatus(true);
      sendCommand("get_ip", {});
    };

    websocket.onclose = function (event) {
      showToast("Disconnected from ESP32", "warning");
      updateConnectionStatus(false);
      setTimeout(connectWebSocket, 3000);
    };

    websocket.onerror = function (error) {
      updateConnectionStatus(false);
    };

    websocket.onmessage = function (event) {
      try {
        const data = JSON.parse(event.data);
        lastUpdateTime = Date.now();
        updateInterface(data);
      } catch (error) {
        console.error("Error parsing JSON:", error);
      }
    };
  }

  // Update interface with data from ESP32
  function updateInterface(data) {
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

    // Handle update info response
    if (data.type === "update_info") {
      handleUpdateInfo(data.data);
    }
  }

  // Handle update information from GitHub
  function handleUpdateInfo(data) {
    const latestVersionElem = document.getElementById("latest-version");
    const startUpdateButton = document.getElementById("start-update-button");
    const updateLittlefsButton = document.getElementById(
      "update-littlefs-button"
    );

    if (data.latest_version) {
      latestVersionElem.textContent = data.latest_version;
    }

    if (data.firmware_url) {
      startUpdateButton.disabled = false;
      startUpdateButton.dataset.url = data.firmware_url;
      startUpdateButton.title = "Update to version " + data.latest_version;
    } else {
      startUpdateButton.disabled = true;
      startUpdateButton.title = "No firmware update available";
    }

    if (data.littlefs_url) {
      updateLittlefsButton.disabled = false;
      updateLittlefsButton.dataset.url = data.littlefs_url;
    } else {
      updateLittlefsButton.disabled = true;
    }

    if (!data.firmware_url && !data.littlefs_url) {
      showToast("No updates available", "warning");
    } else {
      showToast("Update check complete", "success");
    }
  }

  // Update OTA status
  function updateOTAStatus(data) {
    const updateProgress = document.getElementById("update-progress");
    const progressBar = document.getElementById("update-progress-bar");
    const progressPercent = document.getElementById("update-percentage");
    const updateStatus = document.getElementById("update-status");
    const startUpdateButton = document.getElementById("start-update-button");
    const updateLittlefsButton = document.getElementById(
      "update-littlefs-button"
    );
    const checkUpdateButton = document.getElementById("check-update-button");

    if (data.updateInProgress) {
      updateProgress.style.display = "block";
      progressBar.style.width = data.updateProgress + "%";
      progressPercent.textContent = data.updateProgress + "%";
      updateStatus.textContent = data.updateStatus || "Updating...";

      startUpdateButton.disabled = true;
      updateLittlefsButton.disabled = true;
      checkUpdateButton.disabled = true;
    } else {
      if (data.updateStatus) {
        updateStatus.textContent = data.updateStatus;
        if (data.updateProgress === 100) {
          if (data.updateType === "firmware") {
            showToast(
              "Firmware update complete! Device will restart.",
              "success"
            );
          } else {
            showToast("LittleFS update complete!", "success");
          }
          setTimeout(() => {
            updateProgress.style.display = "none";
            progressBar.style.width = "0%";
            progressPercent.textContent = "0%";
          }, 3000);
        } else if (
          data.updateStatus.includes("error") ||
          data.updateStatus.includes("failed")
        ) {
          showToast("Update failed: " + data.updateStatus, "error");
        }
      }

      startUpdateButton.disabled = false;
      updateLittlefsButton.disabled = false;
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

  // Send command to ESP32
  function sendCommand(type, data) {
    if (websocket && websocket.readyState === WebSocket.OPEN) {
      const command = {
        type: type,
        data: data,
        timestamp: Date.now(),
      };
      websocket.send(JSON.stringify(command));
    } else {
      showToast("No connection to ESP32", "warning");
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
          sendCommand("check_updates", {});
          this.disabled = false;
        }, 1000);
      });

    // Start firmware update button
    document
      .getElementById("start-update-button")
      .addEventListener("click", function () {
        const url = this.dataset.url;
        if (url) {
          showConfirmation(
            "Update Firmware",
            "Are you sure you want to update the firmware? The device will restart after update. Do not power off during update!",
            function () {
              sendCommand("update_firmware", { url: url });
              showToast("Starting firmware update...", "success");
            }
          );
        }
      });

    // Update LittleFS button
    const updateLittlefsButton = document.createElement("button");
    updateLittlefsButton.id = "update-littlefs-button";
    updateLittlefsButton.className = "btn btn-update btn-littlefs";
    updateLittlefsButton.innerHTML =
      '<i class="fas fa-hdd"></i> Update LittleFS';
    updateLittlefsButton.disabled = true;

    document.querySelector(".update-actions").appendChild(updateLittlefsButton);

    updateLittlefsButton.addEventListener("click", function () {
      const url = this.dataset.url;
      if (url) {
        showConfirmation(
          "Update LittleFS",
          "Are you sure you want to update the filesystem? This will replace all web files. The device will not restart.",
          function () {
            sendCommand("update_littlefs", { url: url });
            showToast("Starting LittleFS update...", "success");
          }
        );
      }
    });

    // Restart button
    document
      .getElementById("restart-button")
      .addEventListener("click", function () {
        showConfirmation(
          "Restart ESP32",
          "Are you sure you want to restart the ESP32? All current operations will be interrupted.",
          function () {
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
          "This will reset all WiFi settings and restart in configuration mode.",
          function () {
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
          "WARNING: This will erase all files from the filesystem! This action cannot be undone.",
          function () {
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
          "This will start calibration for all motors.",
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
      if (websocket && websocket.readyState === WebSocket.OPEN) {
        sendCommand("get_ip", {});
      }
    }
  }, 5000);

  // Initialize
  initializeEventListeners();
  connectWebSocket();
});
