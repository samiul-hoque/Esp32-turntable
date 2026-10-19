// Pure motion math and validation, shared by the firmware and the native unit tests.
// Formulas and warning text are kept identical to the v1 firmware (legacy/).
#pragma once
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"

struct RotateRequest {
  int angle = 0;          // degrees per repetition
  int timeMs = 0;         // movement time per repetition
  bool reverse = false;
  int repetitions = 1;
  int repDelayMs = 0;
  float accelRpm = DEFAULT_ACCELERATION_RPM_S;
  float decelRpm = DEFAULT_DECELERATION_RPM_S;
};

struct MotionPlan {
  long steps = 0;               // signed steps per repetition
  float speedStepsPerSec = 0;
  float speedRpm = 0;           // after clamping
  float accelRpm = 0;           // after clamping
  float decelRpm = 0;           // after clamping
  float accelStepsPerSec2 = 0;
  char warnings[320] = "";      // v1 warning text, empty if none
};

inline float rpmToStepsPerSec(float rpm) {
  return rpm * stepsPerRev * gearRatio / 60.0;
}

inline float stepsPerSecToRpm(float stepsPerSec) {
  return stepsPerSec * 60.0 / (stepsPerRev * gearRatio);
}

// Same fallbacks as v1 handleRotate(). Returns false (with a reason) only for
// input v1 could never execute sensibly.
inline bool sanitizeRequest(RotateRequest& r, const char** error) {
  if (r.repetitions < 1) r.repetitions = 1;
  if (r.repDelayMs < 0) r.repDelayMs = 0;
  if (!(r.accelRpm > 0)) r.accelRpm = DEFAULT_ACCELERATION_RPM_S;
  if (!(r.decelRpm > 0)) r.decelRpm = DEFAULT_DECELERATION_RPM_S;
  if (r.timeMs <= 0) {
    if (error) *error = "time must be greater than 0";
    return false;
  }
  return true;
}

// Mirrors the first half of v1 rotateStepper(). Returns false when the angle
// rounds to zero steps (v1 silently did nothing in that case).
inline bool planMove(const RotateRequest& r, MotionPlan& p) {
  p = MotionPlan();
  int steps = (r.angle * stepsPerRev * gearRatio) / 360;
  if (steps == 0) return false;
  if (r.reverse) steps = -steps;
  p.steps = steps;

  float durationSec = r.timeMs / 1000.0;
  float requiredSpeed = fabsf((float)steps) / durationSec;
  float requiredSpeedRpm = stepsPerSecToRpm(requiredSpeed);
  float accelRpm = r.accelRpm;
  float decelRpm = r.decelRpm;

  size_t n = 0;
  if (requiredSpeedRpm > MAX_SPEED_RPM) {
    n += snprintf(p.warnings + n, sizeof(p.warnings) - n,
                  "WARNING: Required speed (%.1f RPM) exceeds maximum (%.1f RPM). ",
                  requiredSpeedRpm, MAX_SPEED_RPM);
    requiredSpeed = rpmToStepsPerSec(MAX_SPEED_RPM);
    requiredSpeedRpm = MAX_SPEED_RPM;
  }
  if (accelRpm > MAX_ACCELERATION_RPM_S && n < sizeof(p.warnings)) {
    n += snprintf(p.warnings + n, sizeof(p.warnings) - n,
                  "WARNING: Acceleration (%.1f RPM/s) exceeds maximum (%.1f RPM/s). ",
                  accelRpm, MAX_ACCELERATION_RPM_S);
    accelRpm = MAX_ACCELERATION_RPM_S;
  }
  if (decelRpm > MAX_ACCELERATION_RPM_S && n < sizeof(p.warnings)) {
    snprintf(p.warnings + n, sizeof(p.warnings) - n,
             "WARNING: Deceleration (%.1f RPM/s) exceeds maximum (%.1f RPM/s). ",
             decelRpm, MAX_ACCELERATION_RPM_S);
    decelRpm = MAX_ACCELERATION_RPM_S;
  }

  p.speedStepsPerSec = requiredSpeed;
  p.speedRpm = requiredSpeedRpm;
  p.accelRpm = accelRpm;
  p.decelRpm = decelRpm;
  p.accelStepsPerSec2 = rpmToStepsPerSec(accelRpm);
  return true;
}

// Upper bound on how long a sequence can take: v1 aborts each rep at 2x its duration.
inline unsigned long maxSequenceMs(const RotateRequest& r) {
  return (unsigned long)r.repetitions * (2UL * r.timeMs) +
         (unsigned long)(r.repetitions - 1) * r.repDelayMs;
}

inline float stepsToDegrees(long steps) {
  return (float)steps * 360.0 / (stepsPerRev * gearRatio);
}
