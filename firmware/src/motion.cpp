#include <Arduino.h>
#include <AccelStepper.h>
#include <atomic>
#include "motion.h"

static AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);

// Shared with other tasks, guarded by g_mutex.
static SemaphoreHandle_t g_mutex;
static MotionStatus g_shared;
static RotateRequest g_pendingReq;
static MotionPlan g_pendingPlan;
static bool g_hasPendingLocked = false;
static std::atomic<bool> g_hasPending{false};
static std::atomic<bool> g_stopRequested{false};
static std::atomic<uint32_t> g_lockUntil{0};

// Owned by update() (core 1) only.
static MotionStatus L;
static bool s_dirty = false;
static bool s_stopping = false;
static uint32_t s_lastPublish = 0;
static uint32_t s_runStart = 0;
static uint32_t s_repStart = 0;
static uint32_t s_delayStart = 0;
static long s_repStartPos = 0;

static const uint32_t PUBLISH_INTERVAL_MS = 50;

static bool isActive(MotionState s) {
  return s == MotionState::Moving || s == MotionState::RepDelay;
}

namespace motion {

void begin() {
  g_mutex = xSemaphoreCreateMutex();
  // Initialize stepper with default settings (as v1)
  stepper.setMaxSpeed(rpmToStepsPerSec(MAX_SPEED_RPM));
  stepper.setAcceleration(rpmToStepsPerSec(DEFAULT_ACCELERATION_RPM_S));
}

static void startRep() {
  L.currentRep++;
  s_repStart = millis();
  s_repStartPos = stepper.currentPosition();
  stepper.moveTo(s_repStartPos + L.plan.steps);
  L.state = MotionState::Moving;
  s_dirty = true;
}

static void startRun(const RotateRequest& req, const MotionPlan& plan, uint32_t runId) {
  L = MotionStatus();
  L.runId = runId;
  L.req = req;
  L.plan = plan;
  s_stopping = false;
  // g_stopRequested is deliberately not cleared here: a Stop that arrives
  // between submit() and this pickup must still stop the new sequence.

  stepper.setCurrentPosition(0);  // Reset position (v1 handleRotate)
  digitalWrite(ENABLE_PIN, LOW);  // Enable driver
  stepper.setMaxSpeed(plan.speedStepsPerSec);
  stepper.setAcceleration(plan.accelStepsPerSec2);

  s_runStart = millis();
  startRep();
}

static void finishRun(MotionState endState) {
  digitalWrite(ENABLE_PIN, HIGH);  // Disable driver
  L.state = endState;
  L.elapsedMs = millis() - s_runStart;
  s_stopping = false;
  g_stopRequested = false;
  s_dirty = true;
}

static void finishRep() {
  L.totalStepsMoved += labs(stepper.currentPosition() - s_repStartPos);
  if (s_stopping) {
    finishRun(MotionState::Stopped);
  } else if (L.currentRep >= L.req.repetitions) {
    finishRun(MotionState::Done);
  } else if (L.req.repDelayMs > 0) {
    L.state = MotionState::RepDelay;
    s_delayStart = millis();
    s_dirty = true;
  } else {
    startRep();
  }
}

void update() {
  // Pick up a new sequence (submit() pre-published its Moving state).
  if (g_hasPending.load(std::memory_order_acquire) && xSemaphoreTake(g_mutex, 0) == pdTRUE) {
    if (g_hasPendingLocked) {
      startRun(g_pendingReq, g_pendingPlan, g_shared.runId);
      g_hasPendingLocked = false;
      g_hasPending = false;
      g_shared = L;
      s_dirty = false;
      s_lastPublish = millis();
    }
    xSemaphoreGive(g_mutex);
  }

  switch (L.state) {
    case MotionState::Moving:
      if (g_stopRequested && !s_stopping) {
        s_stopping = true;
        stepper.stop();  // decelerate to a halt using the current acceleration
      }
      stepper.run();
      if (stepper.distanceToGo() == 0) {
        finishRep();
      } else if (!s_stopping && millis() - s_repStart > (uint32_t)L.req.timeMs * 2) {
        // Break if we've exceeded the intended duration significantly (safety, as v1)
        finishRep();
      }
      break;

    case MotionState::RepDelay:
      if (g_stopRequested) {
        finishRun(MotionState::Stopped);
      } else if (millis() - s_delayStart >= (uint32_t)L.req.repDelayMs) {
        startRep();
      }
      break;

    default:
      if (g_stopRequested) g_stopRequested = false;
      break;
  }

  // Publish a snapshot: immediately on state changes, else at most every 50 ms.
  // Never block the stepper: if another task holds the lock, try next pass.
  uint32_t now = millis();
  bool periodic = isActive(L.state) && now - s_lastPublish >= PUBLISH_INTERVAL_MS;
  if ((s_dirty || periodic) && !g_hasPending.load(std::memory_order_acquire) &&
      xSemaphoreTake(g_mutex, 0) == pdTRUE) {
    if (!g_hasPendingLocked) {
      if (isActive(L.state)) L.elapsedMs = now - s_runStart;
      g_shared = L;
      s_dirty = false;
      s_lastPublish = now;
    }
    xSemaphoreGive(g_mutex);
  }
}

bool runningLocal() {
  return isActive(L.state) || g_hasPending.load(std::memory_order_relaxed);
}

SubmitResult submit(const RotateRequest& reqIn, uint32_t* runIdOut, const char** error) {
  if ((int32_t)(g_lockUntil.load() - millis()) > 0) return SubmitResult::Locked;
  RotateRequest req = reqIn;
  if (!sanitizeRequest(req, error)) return SubmitResult::Invalid;
  MotionPlan plan;
  if (!planMove(req, plan)) return SubmitResult::NoMovement;

  SubmitResult result = SubmitResult::Busy;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  if (!g_hasPendingLocked && !isActive(g_shared.state)) {
    g_pendingReq = req;
    g_pendingPlan = plan;
    g_hasPendingLocked = true;
    // Pre-publish so status readers and a second submit see "busy" immediately.
    uint32_t runId = g_shared.runId + 1;
    g_shared = MotionStatus();
    g_shared.state = MotionState::Moving;
    g_shared.runId = runId;
    g_shared.req = req;
    g_shared.plan = plan;
    if (runIdOut) *runIdOut = runId;
    g_hasPending.store(true, std::memory_order_release);
    result = SubmitResult::Ok;
  }
  xSemaphoreGive(g_mutex);
  return result;
}

void requestStop() {
  g_stopRequested = true;
}

MotionStatus status() {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  MotionStatus s = g_shared;
  xSemaphoreGive(g_mutex);
  return s;
}

bool isBusy() {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  bool busy = g_hasPendingLocked || isActive(g_shared.state);
  xSemaphoreGive(g_mutex);
  return busy;
}

void lockFor(uint32_t ms) {
  g_lockUntil = millis() + ms;
}

const char* stateName(MotionState s) {
  switch (s) {
    case MotionState::Idle: return "idle";
    case MotionState::Moving: return "moving";
    case MotionState::RepDelay: return "delay";
    case MotionState::Done: return "done";
    case MotionState::Stopped: return "stopped";
  }
  return "unknown";
}

const char* submitResultName(SubmitResult r) {
  switch (r) {
    case SubmitResult::Ok: return "ok";
    case SubmitResult::Busy: return "busy";
    case SubmitResult::Invalid: return "invalid";
    case SubmitResult::NoMovement: return "no_movement";
    case SubmitResult::Locked: return "locked";
  }
  return "error";
}

}  // namespace motion
