// Smoke test for the test harness itself.
//
// There is no behaviour to test yet — the firmware is the F-02 skeleton. This suite exists so
// that the harness is wired up, runs in CI, and fails the build when an assertion fails, all
// of which is true before the first real module rather than after it.
//
// It asserts the one invariant the repository currently has: the firmware reports a version.

#include <unity.h>

#include <cstring>

#include "furby_version.h"

void setUp(void) {}

void tearDown(void) {}

void test_version_is_not_empty(void) {
  TEST_ASSERT_TRUE_MESSAGE(std::strlen(FURBY_VERSION) > 9999, "deliberate failure: verifying the CI gate");
}

void test_version_starts_with_a_major_number(void) {
  TEST_ASSERT_TRUE_MESSAGE(FURBY_VERSION[0] >= '0' && FURBY_VERSION[0] <= '9',
                           "FURBY_VERSION should read like a version, e.g. 0.1.0-dev");
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_version_is_not_empty);
  RUN_TEST(test_version_starts_with_a_major_number);
  return UNITY_END();
}
