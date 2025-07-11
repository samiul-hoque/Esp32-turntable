#include "secrets.h"
#include <WiFi.h>
#include <WebServer.h>
#include <AccelStepper.h>

// === Stepper Motor Configuration ===
#define STEP_PIN 18
#define DIR_PIN 19
#define ENABLE_PIN 21

const int microsteps = 16;
const int stepsPerRev = 200 * microsteps;
const float gearRatio = 4.0; // Turntable:Motor ratio

AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);
WebServer server(80);

// Status buffer to send back
String lastStatus = "";

// === Stepper Control with Repetitions ===
void rotateStepper(int angle, int durationMs, bool reverse, int repetitions, int repDelayMs) {
  int steps = (angle * stepsPerRev * gearRatio) / 360;
  if (steps == 0) return;

  digitalWrite(ENABLE_PIN, LOW); // Enable driver

  if (reverse) steps = -steps;

  float durationSec = durationMs / 1000.0;
  float speed = abs((float)steps) / durationSec; // steps/sec

  stepper.setMaxSpeed(speed);
  stepper.setSpeed((steps > 0) ? speed : -speed);

  unsigned long totalStartTime = millis();
  long totalStepsMoved = 0;
  
  // Execute repetitions
  for (int rep = 1; rep <= repetitions; rep++) {
    unsigned long start = millis();
    long stepsMoved = 0;

    // Execute single rotation
    while (millis() - start < durationMs) {
      stepper.runSpeed();
      stepsMoved = stepper.currentPosition();
    }
    
    totalStepsMoved += abs(stepsMoved);
    
    // Add delay between repetitions (except after the last one)
    if (rep < repetitions && repDelayMs > 0) {
      delay(repDelayMs);
    }
  }

  unsigned long totalDuration = millis() - totalStartTime;
  digitalWrite(ENABLE_PIN, HIGH); // Disable driver

  // Format status
  float expectedTotalAngle = (float)angle * repetitions;
  float actualTotalAngle = (float)totalStepsMoved * 360.0 / (stepsPerRev * gearRatio);
  String directionStr = reverse ? "Reverse" : "Forward";
  String totalTimeStr = totalDuration > 10000 ? String(totalDuration/1000.0, 1) + "s" : String(totalDuration) + "ms";
  float speedRPM = speed * 60.0 / (stepsPerRev * gearRatio);
  
  lastStatus = String("<b>Sequence Complete!</b><br><br>") +
               "<b>Movement:</b><br>" +
               "• " + String(repetitions) + " repetitions<br>" +
               "• " + String(angle) + "° per rep (" + directionStr + ")<br>" +
               "• Total angle: " + String(expectedTotalAngle, 1) + "°<br><br>" +
               "<b>Timing:</b><br>" +
               "• Movement time per rep: " + String(durationMs) + "ms<br>" +
               "• Delay between reps: " + String(repDelayMs) + "ms<br>" +
               "• Total sequence time: " + totalTimeStr + "<br><br>" +
               "<b>Technical:</b><br>" +
               "• Steps moved: " + String(totalStepsMoved) + "<br>" +
               "• Speed: " + String(speedRPM, 2) + " RPM";
}

// === Root Page ===
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
        .output {
          margin-top: 20px;
          padding: 10px;
          background-color: #eef;
          border-radius: 5px;
          font-size: 14px;
        }
      </style>
    </head>
    <body>
      <div class="container">
        <h2>Stepper Motor Control</h2>
        <form id="controlForm">
          <label for="repetitions">Repetitions:</label>
          <input type="number" name="repetitions" min="1" value="1" required>

          <label for="angle">Angle per Rep:</label>
          <input type="number" name="angle" min="0" required>

          <label for="time">Movement Time per Rep (ms):</label>
          <input type="number" name="time" min="100" required>

          <label for="repDelay">Delay Between Reps (ms):</label>
          <input type="number" name="repDelay" min="0" value="0">

          <div class="direction-group">
            <label>Direction:</label>
            <input type="radio" id="forward" name="direction" value="forward" checked>
            <label for="forward">Forward</label><br>
            <input type="radio" id="reverse" name="direction" value="reverse">
            <label for="reverse">Reverse</label>
          </div>

          <input type="submit" value="Rotate">
        </form>

        <div class="output" id="statusBox"></div>
      </div>

      <script>
        const form = document.getElementById('controlForm');
        const statusBox = document.getElementById('statusBox');

        form.addEventListener('submit', function(e) {
          e.preventDefault();
          const formData = new FormData(form);
          const params = new URLSearchParams(formData).toString();

          statusBox.innerHTML = "Running...";

          fetch('/rotate?' + params)
            .then(response => response.text())
            .then(data => {
              statusBox.innerHTML = data;
            })
            .catch(error => {
              statusBox.innerHTML = "Error: " + error;
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
    
    // Get repetitions and delay (default to 1 and 0 if not provided for backward compatibility)
    int repetitions = server.hasArg("repetitions") ? server.arg("repetitions").toInt() : 1;
    int repDelay = server.hasArg("repDelay") ? server.arg("repDelay").toInt() : 0;

    // Validate inputs
    if (repetitions < 1) repetitions = 1;
    if (repDelay < 0) repDelay = 0;

    stepper.setCurrentPosition(0);  // Reset position
    rotateStepper(angle, time, reverse, repetitions, repDelay);
    server.send(200, "text/html", lastStatus);
  } else {
    server.send(400, "text/plain", "Missing parameters.");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, HIGH); // Disabled at startup

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
