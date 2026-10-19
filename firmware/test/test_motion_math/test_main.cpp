// Native unit tests: pio test -e native
// Expected values are what the v1 firmware (legacy/) computes for the same input.
#include <string.h>
#include <unity.h>
#include "motion_math.h"

void setUp() {}
void tearDown() {}

static RotateRequest req(int angle, int timeMs) {
  RotateRequest r;
  r.angle = angle;
  r.timeMs = timeMs;
  return r;
}

void test_rpm_conversion_roundtrip() {
  // 1 RPM = 3200 steps/rev * 4 gear / 60 s
  TEST_ASSERT_FLOAT_WITHIN(0.001, 213.333, rpmToStepsPerSec(1.0));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 60.0, stepsPerSecToRpm(rpmToStepsPerSec(60.0)));
}

void test_readme_scenario() {
  // README example: 90 deg, 2000 ms, reverse, 3 reps, 500 ms
  RotateRequest r = req(90, 2000);
  r.reverse = true;
  r.repetitions = 3;
  r.repDelayMs = 500;
  MotionPlan p;
  TEST_ASSERT_TRUE(planMove(r, p));
  TEST_ASSERT_EQUAL(-3200, p.steps);          // quarter turn of the platform
  TEST_ASSERT_FLOAT_WITHIN(0.01, 1600.0, p.speedStepsPerSec);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 7.5, p.speedRpm);
  TEST_ASSERT_EQUAL_STRING("", p.warnings);
  TEST_ASSERT_EQUAL(3 * 4000 + 2 * 500, maxSequenceMs(r));
}

void test_speed_clamped_with_v1_warning() {
  MotionPlan p;
  TEST_ASSERT_TRUE(planMove(req(360, 100), p));  // 1 rev in 0.1 s = 600 RPM
  TEST_ASSERT_FLOAT_WITHIN(0.01, MAX_SPEED_RPM, p.speedRpm);
  TEST_ASSERT_FLOAT_WITHIN(0.1, rpmToStepsPerSec(MAX_SPEED_RPM), p.speedStepsPerSec);
  TEST_ASSERT_EQUAL_STRING("WARNING: Required speed (600.0 RPM) exceeds maximum (60.0 RPM). ", p.warnings);
}

void test_accel_and_decel_clamped() {
  RotateRequest r = req(90, 2000);
  r.accelRpm = 100;
  r.decelRpm = 45.5;
  MotionPlan p;
  TEST_ASSERT_TRUE(planMove(r, p));
  TEST_ASSERT_FLOAT_WITHIN(0.01, MAX_ACCELERATION_RPM_S, p.accelRpm);
  TEST_ASSERT_FLOAT_WITHIN(0.01, MAX_ACCELERATION_RPM_S, p.decelRpm);
  TEST_ASSERT_NOT_NULL(strstr(p.warnings, "Acceleration (100.0 RPM/s)"));
  TEST_ASSERT_NOT_NULL(strstr(p.warnings, "Deceleration (45.5 RPM/s)"));
}

void test_zero_angle_does_not_move() {
  MotionPlan p;
  TEST_ASSERT_FALSE(planMove(req(0, 1000), p));
}

void test_sanitize_matches_v1_defaults() {
  RotateRequest r = req(90, 1000);
  r.repetitions = 0;
  r.repDelayMs = -5;
  r.accelRpm = 0;
  r.decelRpm = -1;
  const char* err = nullptr;
  TEST_ASSERT_TRUE(sanitizeRequest(r, &err));
  TEST_ASSERT_EQUAL(1, r.repetitions);
  TEST_ASSERT_EQUAL(0, r.repDelayMs);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_ACCELERATION_RPM_S, r.accelRpm);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_DECELERATION_RPM_S, r.decelRpm);
}

void test_sanitize_rejects_zero_time() {
  RotateRequest r = req(90, 0);
  const char* err = nullptr;
  TEST_ASSERT_FALSE(sanitizeRequest(r, &err));
  TEST_ASSERT_NOT_NULL(err);
}

void test_steps_to_degrees() {
  TEST_ASSERT_FLOAT_WITHIN(0.001, 90.0, stepsToDegrees(3200));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 360.0, stepsToDegrees(12800));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rpm_conversion_roundtrip);
  RUN_TEST(test_readme_scenario);
  RUN_TEST(test_speed_clamped_with_v1_warning);
  RUN_TEST(test_accel_and_decel_clamped);
  RUN_TEST(test_zero_angle_does_not_move);
  RUN_TEST(test_sanitize_matches_v1_defaults);
  RUN_TEST(test_sanitize_rejects_zero_time);
  RUN_TEST(test_steps_to_degrees);
  return UNITY_END();
}
