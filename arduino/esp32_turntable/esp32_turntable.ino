#include "secrets.h"
#include <WiFi.h>
#include <WebServer.h>
#include <AccelStepper.h>


// === Wi-Fi Credentials ===
const char* ssid = "Vinci";
const char* password = "asdfzxcv";

// === Stepper Motor Configuration ===
#define STEP_PIN 18
#define DIR_PIN 19
#define ENABLE_PIN 21
const int stepsPerRev = 200*16;

AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);
WebServer server(80);

// === Stepper Control ===
void rotateStepper(int angle, int durationMs, bool reverse) {
  int steps = (angle * stepsPerRev) / 360;
  if (steps == 0) return;

  digitalWrite(ENABLE_PIN, LOW); // Enable driver

  if (reverse) steps = -steps;
  long targetPos = stepper.currentPosition() + steps;
  stepper.moveTo(targetPos);

  // Estimate speed from duration
  float speed = abs((float)steps / ((float)durationMs / 1000.0));
  speed = constrain(speed, 10, 1000); // Avoid too slow or too fast
  stepper.setMaxSpeed(speed);

  while (stepper.distanceToGo() != 0) {
    stepper.run();
  }

  digitalWrite(ENABLE_PIN, HIGH); // Disable driver
}

// === Root Page (HTML + CSS + JS) ===
void handleRoot() {
  server.send(200, "text/html", R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Stepper Motor Control</title>
      <style>
        body {
          font-family: Arial, sans-serif;
          background-color: #f4f4f4;
          display: flex;
          justify-content: center;
          align-items: center;
          height: 100vh;
        }
        .container {
          background: #fff;
          padding: 30px;
          border-radius: 10px;
          box-shadow: 0 0 15px rgba(0,0,0,0.2);
          width: 300px;
          position: relative;
        }
        h2 { text-align: center; }
        label { margin: 10px 0 5px; display: block; }
        input[type="number"], input[type="submit"] {
          width: 100%;
          padding: 8px;
          margin-bottom: 10px;
          border-radius: 4px;
          border: 1px solid #ccc;
        }
        .direction-group {
          margin-bottom: 15px;
        }
        .direction-group input {
          margin-right: 5px;
        }
        input[type="submit"] {
          background-color: #4CAF50;
          color: white;
          border: none;
          cursor: pointer;
        }
        input[type="submit"]:hover {
          background-color: #45a049;
        }
        .spinner {
          display: none;
          position: absolute;
          top: 50%;
          left: 50%;
          transform: translate(-50%, -50%);
        }
        .spinner div {
          width: 24px;
          height: 24px;
          border: 4px solid #4CAF50;
          border-top: 4px solid transparent;
          border-radius: 50%;
          animation: spin 1s linear infinite;
        }
        @keyframes spin {
          0% { transform: rotate(0deg); }
          100% { transform: rotate(360deg); }
        }
      </style>
    </head>
    <body>
      <div class="container">
        <h2>Stepper Motor Control</h2>
        <form id="controlForm">
          <label for="angle">Angle:</label>
          <input type="number" name="angle" min="0" required>

          <label for="time">Time (ms):</label>
          <input type="number" name="time" min="100" required>

          <div class="direction-group">
            <label>Direction:</label>
            <input type="radio" id="forward" name="direction" value="forward" checked>
            <label for="forward">Forward</label><br>
            <input type="radio" id="reverse" name="direction" value="reverse">
            <label for="reverse">Reverse</label>
          </div>

          <input type="submit" value="Rotate">
        </form>

        <div class="spinner" id="spinner">
          <div></div>
        </div>
      </div>

      <script>
        const form = document.getElementById('controlForm');
        const spinner = document.getElementById('spinner');

        form.addEventListener('submit', function(e) {
          e.preventDefault();

          const formData = new FormData(form);
          const params = new URLSearchParams(formData).toString();

          spinner.style.display = 'block';

          fetch('/rotate?' + params)
            .then(response => response.text())
            .then(data => {
              spinner.style.display = 'none';
              alert("Rotation complete.");
            })
            .catch(error => {
              spinner.style.display = 'none';
              alert("Error: " + error);
            });
        });
      </script>
    </body>
    </html>
  )rawliteral");
}

// === Handle Rotation Request ===
void handleRotate() {
  if (server.hasArg("angle") && server.hasArg("time") && server.hasArg("direction")) {
    int angle = server.arg("angle").toInt();
    int time = server.arg("time").toInt();
    String dir = server.arg("direction");
    bool reverse = (dir == "reverse");

    rotateStepper(angle, time, reverse);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing parameters.");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, HIGH); // Disabled at startup

  stepper.setMaxSpeed(1000);
  stepper.setAcceleration(500);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  server.on("/", handleRoot);
  server.on("/rotate", handleRotate);
  server.begin();
  Serial.println("Web server started.");
}

void loop() {
  server.handleClient();
}
