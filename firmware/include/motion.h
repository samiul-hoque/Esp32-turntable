// Non-blocking motion engine. motion::update() runs in loop() on core 1 and is the
// only code that touches the stepper. Everything else is thread-safe and may be
// called from the network task or BLE callbacks.
#pragma once
#include <stdint.h>
#include "motion_math.h"

enum class MotionState : uint8_t {
  Idle,      // nothing has run since boot
  Moving,
  RepDelay,  // pausing between repetitions (driver stays enabled, as in v1)
  Done,      // last sequence completed
  Stopped,   // last sequence was stopped by the user
};

struct MotionStatus {
  MotionState state = MotionState::Idle;
  uint32_t runId = 0;           // increments for every accepted sequence
  RotateRequest req;
  MotionPlan plan;
  int currentRep = 0;           // 1-based rep in progress (or last finished)
  long totalStepsMoved = 0;
  uint32_t elapsedMs = 0;       // total sequence time so far / final
};

enum class SubmitResult : uint8_t { Ok, Busy, Invalid, NoMovement, Locked };

namespace motion {
void begin();
void update();                  // loop() only
bool runningLocal();            // loop() only: true while the stepper needs servicing

SubmitResult submit(const RotateRequest& req, uint32_t* runIdOut, const char** error);
void requestStop();
MotionStatus status();          // consistent snapshot
bool isBusy();
void lockFor(uint32_t ms);      // refuse new sequences for ms (lease; 0 = unlock), e.g. during a firmware update

const char* stateName(MotionState s);
const char* submitResultName(SubmitResult r);
}
