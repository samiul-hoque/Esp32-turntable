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

// === Safety Limits (modify these values in firmware) ===
const float MAX_SPEED_RPM = 60.0;  // Maximum allowed speed in RPM
const float MAX_ACCELERATION_RPM_S = 30.0;  // Maximum acceleration in RPM/s
const float DEFAULT_ACCELERATION_RPM_S = 10.0;  // Default acceleration if not specified
const float DEFAULT_DECELERATION_RPM_S = 10.0;  // Default deceleration if not specified

AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);
WebServer server(80);

// Status buffer to send back
String lastStatus = "";

// === Helper Functions ===
float rpmToStepsPerSec(float rpm) {
  return rpm * stepsPerRev * gearRatio / 60.0;
}

float stepsPerSecToRpm(float stepsPerSec) {
  return stepsPerSec * 60.0 / (stepsPerRev * gearRatio);
}

// === Enhanced Stepper Control with Acceleration/Deceleration ===
void rotateStepper(int angle, int durationMs, bool reverse, int repetitions, int repDelayMs, float accelRpm, float decelRpm) {
  int steps = (angle * stepsPerRev * gearRatio) / 360;
  if (steps == 0) return;

  digitalWrite(ENABLE_PIN, LOW); // Enable driver

  if (reverse) steps = -steps;

  float durationSec = durationMs / 1000.0;
  float requiredSpeed = abs((float)steps) / durationSec; // steps/sec
  float requiredSpeedRpm = stepsPerSecToRpm(requiredSpeed);

  // Safety checks
  String warnings = "";
  if (requiredSpeedRpm > MAX_SPEED_RPM) {
    warnings += String("WARNING: Required speed (") + String(requiredSpeedRpm, 1) + String(" RPM) exceeds maximum (") + String(MAX_SPEED_RPM, 1) + String(" RPM). ");
    requiredSpeed = rpmToStepsPerSec(MAX_SPEED_RPM);
    requiredSpeedRpm = MAX_SPEED_RPM;
  }
  
  if (accelRpm > MAX_ACCELERATION_RPM_S) {
    warnings += String("WARNING: Acceleration (") + String(accelRpm, 1) + String(" RPM/s) exceeds maximum (") + String(MAX_ACCELERATION_RPM_S, 1) + String(" RPM/s). ");
    accelRpm = MAX_ACCELERATION_RPM_S;
  }
  
  if (decelRpm > MAX_ACCELERATION_RPM_S) {
    warnings += String("WARNING: Deceleration (") + String(decelRpm, 1) + String(" RPM/s) exceeds maximum (") + String(MAX_ACCELERATION_RPM_S, 1) + String(" RPM/s). ");
    decelRpm = MAX_ACCELERATION_RPM_S;
  }

  // Convert acceleration to steps/sec²
  float accelStepsPerSec2 = rpmToStepsPerSec(accelRpm);
  float decelStepsPerSec2 = rpmToStepsPerSec(decelRpm);

  // Configure stepper with acceleration
  stepper.setMaxSpeed(requiredSpeed);
  stepper.setAcceleration(accelStepsPerSec2);

  unsigned long totalStartTime = millis();
  long totalStepsMoved = 0;
  
  // Execute repetitions
  for (int rep = 1; rep <= repetitions; rep++) {
    unsigned long start = millis();
    
    // Set target position for this repetition
    long targetSteps = stepper.currentPosition() + steps;
    stepper.moveTo(targetSteps);
    
    // For deceleration, we need to handle it manually since AccelStepper doesn't support different accel/decel
    // We'll use a simple approach: use acceleration for the entire move, but could be enhanced
    
    // Execute single rotation with acceleration
    while (stepper.distanceToGo() != 0) {
      stepper.run();
      
      // Break if we've exceeded the intended duration significantly (safety)
      if (millis() - start > durationMs * 2) break;
    }
    
    unsigned long repDuration = millis() - start;
    totalStepsMoved += abs(stepper.currentPosition() - (targetSteps - steps));
    
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
  
  lastStatus = String("<b>Sequence Complete!</b><br><br>");
  
  if (warnings.length() > 0) {
    lastStatus += String("<b style='color: orange;'>WARNING: ") + warnings + String("</b><br><br>");
  }
  
  lastStatus += String("<b>Movement:</b><br>");
  lastStatus += String("• ") + String(repetitions) + String(" repetitions<br>");
  lastStatus += String("• ") + String(angle) + String("° per rep (") + directionStr + String(")<br>");
  lastStatus += String("• Total angle: ") + String(expectedTotalAngle, 1) + String("°<br><br>");
  lastStatus += String("<b>Timing:</b><br>");
  lastStatus += String("• Movement time per rep: ") + String(durationMs) + String("ms<br>");
  lastStatus += String("• Delay between reps: ") + String(repDelayMs) + String("ms<br>");
  lastStatus += String("• Total sequence time: ") + totalTimeStr + String("<br><br>");
  lastStatus += String("<b>Motion Profile:</b><br>");
  lastStatus += String("• Target speed: ") + String(requiredSpeedRpm, 2) + String(" RPM<br>");
  lastStatus += String("• Acceleration: ") + String(accelRpm, 1) + String(" RPM/s<br>");
  lastStatus += String("• Deceleration: ") + String(decelRpm, 1) + String(" RPM/s<br><br>");
  lastStatus += String("<b>Technical:</b><br>");
  lastStatus += String("• Steps moved: ") + String(totalStepsMoved) + String("<br>");
  lastStatus += String("• Actual angle: ") + String(actualTotalAngle, 1) + String("°");
}

// === Root Page ===
void handleRoot() {
  String html = String(R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Auleek Turntable Control</title>
      <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 1024 1024'><path fill='%23fff' d='M256 256h512v96H256zM192 768l208-352h224l208 352H704L512 448 320 768z'/></svg>">
      <style>
        body {
          font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Roboto', 'Helvetica Neue', Arial, sans-serif;
          background-color: #000000;
          display: flex;
          justify-content: center;
          align-items: center;
          min-height: 100vh;
          margin: 0;
          padding: 20px;
          box-sizing: border-box;
        }
        .container {
          background: #1a1a1a;
          padding: 30px;
          border-radius: 10px;
          box-shadow: 0 0 25px rgba(255,255,255,0.1);
          width: 400px;
          max-width: 90vw;
          border: 1px solid #333;
        }
        .header {
          display: flex;
          align-items: center;
          justify-content: center;
          margin-bottom: 25px;
          gap: 15px;
        }
        .logo {
          width: 32px;
          height: 32px;
          flex-shrink: 0;
        }
        h2 { 
          color: #ffffff; 
          font-weight: 700;
          margin: 0;
        }
        .limits-info {
          background-color: #2a2a2a;
          border-left: 4px solid #ffffff;
          padding: 15px;
          margin-bottom: 20px;
          font-size: 12px;
          border-radius: 4px;
          color: #ffffff;
        }
        .limits-info h4 {
          margin: 0 0 8px 0;
          color: #ffffff;
          font-weight: 600;
        }
        label { 
          margin: 10px 0 5px; 
          display: block; 
          font-weight: 600;
          color: #ffffff;
        }
        input[type="number"], input[type="submit"] {
          width: 100%;
          padding: 10px;
          margin-bottom: 10px;
          border-radius: 4px;
          border: 1px solid #555;
          box-sizing: border-box;
          background-color: #2a2a2a;
          color: #ffffff;
          font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Roboto', 'Helvetica Neue', Arial, sans-serif;
        }
        input[type="number"]:focus {
          outline: none;
          border-color: #ffffff;
          background-color: #333;
        }
        input[type="number"]::placeholder {
          color: #888;
        }
        .section {
          background-color: #2a2a2a;
          padding: 15px;
          margin: 15px 0;
          border-radius: 5px;
          border: 1px solid #444;
        }
        .section h3 {
          margin: 0 0 15px 0;
          color: #ffffff;
          font-size: 14px;
          font-weight: 600;
          text-transform: uppercase;
          letter-spacing: 0.5px;
        }
        .direction-group {
          margin-bottom: 10px;
        }
        .direction-group input[type="radio"] {
          width: auto;
          margin-right: 8px;
          margin-bottom: 5px;
          accent-color: #ffffff;
        }
        .direction-group label {
          display: inline;
          margin: 0;
          font-weight: 400;
          color: #ffffff;
        }
        .radio-option {
          margin-bottom: 8px;
        }
        input[type="submit"] {
          background-color: #ffffff;
          color: #000000;
          border: none;
          cursor: pointer;
          font-size: 16px;
          font-weight: 700;
          text-transform: uppercase;
          letter-spacing: 0.5px;
          transition: all 0.3s ease;
        }
        input[type="submit"]:hover {
          background-color: #e0e0e0;
          transform: translateY(-1px);
        }
        .output {
          margin-top: 20px;
          padding: 15px;
          background-color: #2a2a2a;
          border-radius: 5px;
          font-size: 14px;
          border: 1px solid #444;
          color: #ffffff;
        }
        .hint {
          font-size: 11px;
          color: #aaa;
          margin-top: -8px;
          margin-bottom: 10px;
        }
      </style>
    </head>
    <body>
      <div class="container">
        <div class="header">
          <svg class="logo" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024">
            <path fill="#fff" d="M256 256h512v96H256zM192 768l208-352h224l208 352H704L512 448 320 768z"/>
          </svg>
          <h2>Auleek Turntable Control</h2>
        </div>
        
        <div class="limits-info">
          <h4>System Limits</h4>
          <div>Max Speed: )rawliteral");
  
  html += String(MAX_SPEED_RPM, 1) + String(R"rawliteral( RPM</div>
          <div>Max Acceleration: )rawliteral");
  
  html += String(MAX_ACCELERATION_RPM_S, 1) + String(R"rawliteral( RPM/s</div>
          <div style="margin-top: 5px; font-style: italic;">Limits can only be changed in firmware</div>
        </div>

        <form id="controlForm">
          <div class="section">
            <h3>Movement Parameters</h3>
            
            <label for="repetitions">Repetitions:</label>
            <input type="number" name="repetitions" min="1" value="1" required>

            <label for="angle">Angle per Rep (degrees):</label>
            <input type="number" name="angle" min="0" required>

            <label for="time">Movement Time per Rep (ms):</label>
            <input type="number" name="time" min="100" required>

            <label for="repDelay">Delay Between Reps (ms):</label>
            <input type="number" name="repDelay" min="0" value="0">
          </div>

          <div class="section">
            <h3>Direction</h3>
            <div class="direction-group">
              <div class="radio-option">
                <input type="radio" id="forward" name="direction" value="forward" checked>
                <label for="forward">Forward</label>
              </div>
              <div class="radio-option">
                <input type="radio" id="reverse" name="direction" value="reverse">
                <label for="reverse">Reverse</label>
              </div>
            </div>
          </div>

          <div class="section">
            <h3>Motion Profile (Optional)</h3>
            
            <label for="acceleration">Acceleration (RPM/s):</label>
            <input type="number" name="acceleration" min="0.1" step="0.1" placeholder=")rawliteral");
  
  html += String(DEFAULT_ACCELERATION_RPM_S, 1) + String(R"rawliteral( (default)">
            <div class="hint">How quickly to reach target speed</div>

            <label for="deceleration">Deceleration (RPM/s):</label>
            <input type="number" name="deceleration" min="0.1" step="0.1" placeholder=")rawliteral");
  
  html += String(DEFAULT_DECELERATION_RPM_S, 1) + String(R"rawliteral( (default)">
            <div class="hint">How quickly to slow down and stop</div>
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
              statusBox.innerHTML = "<span style='color: red;'>Error: " + error + "</span>";
            });
        });
      </script>
    </body>
    </html>
  )rawliteral");
  
  server.send(200, "text/html", html);
}

// === Handle Rotation Request ===
void handleRotate() {
  if (server.hasArg("angle") && server.hasArg("time") && server.hasArg("direction")) {
    int angle = server.arg("angle").toInt();
    int time = server.arg("time").toInt();
    String dir = server.arg("direction");
    bool reverse = (dir == "reverse");
    
    // Get repetitions and delay (default to 1 and 0 if not provided)
    int repetitions = server.hasArg("repetitions") ? server.arg("repetitions").toInt() : 1;
    int repDelay = server.hasArg("repDelay") ? server.arg("repDelay").toInt() : 0;
    
    // Get acceleration and deceleration (use defaults if not provided)
    float accelRpm = server.hasArg("acceleration") && server.arg("acceleration").length() > 0 ? 
                     server.arg("acceleration").toFloat() : DEFAULT_ACCELERATION_RPM_S;
    float decelRpm = server.hasArg("deceleration") && server.arg("deceleration").length() > 0 ? 
                     server.arg("deceleration").toFloat() : DEFAULT_DECELERATION_RPM_S;

    // Validate inputs
    if (repetitions < 1) repetitions = 1;
    if (repDelay < 0) repDelay = 0;
    if (accelRpm <= 0) accelRpm = DEFAULT_ACCELERATION_RPM_S;
    if (decelRpm <= 0) decelRpm = DEFAULT_DECELERATION_RPM_S;

    stepper.setCurrentPosition(0);  // Reset position
    rotateStepper(angle, time, reverse, repetitions, repDelay, accelRpm, decelRpm);
    server.send(200, "text/html", lastStatus);
  } else {
    server.send(400, "text/plain", "Missing parameters.");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, HIGH); // Disabled at startup

  // Initialize stepper with default settings
  stepper.setMaxSpeed(rpmToStepsPerSec(MAX_SPEED_RPM));
  stepper.setAcceleration(rpmToStepsPerSec(DEFAULT_ACCELERATION_RPM_S));

  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
  Serial.println("System Limits:");
  Serial.println("  Max Speed: " + String(MAX_SPEED_RPM, 1) + " RPM");
  Serial.println("  Max Acceleration: " + String(MAX_ACCELERATION_RPM_S, 1) + " RPM/s");

  server.on("/", handleRoot);
  server.on("/rotate", handleRotate);
  server.begin();
  Serial.println("Web server started.");
}

void loop() {
  server.handleClient();
}
