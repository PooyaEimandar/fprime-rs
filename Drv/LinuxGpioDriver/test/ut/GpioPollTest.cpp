// ======================================================================
// \title  GpioPollTest.cpp
// \brief  Tests for the GPIO interrupt poll-result decision
//
// Drv/LinuxGpioDriver had no unit test of any kind. The interrupt loop needs
// a real gpiochip and cannot run in CI, but the decision it makes on each
// poll() result does not -- so that decision was pulled out into
// classify_poll_result, which is free, pure and lives in the platform-
// independent LinuxGpioDriverCommon.cpp.
//
// Two behaviours are pinned here because the loop used to get both wrong:
//
//  * EINTR is a signal arriving during poll(), not a fault. It was reported
//    as WARNING_HI on every occurrence, which on a signal-heavy system is a
//    stream of events describing normal operation.
//  * A persistent error -- a closed or invalid fd -- makes poll() return
//    immediately. The loop logged and continued, so it span at CPU speed and
//    emitted one event per iteration. The caller now paces itself.
// ======================================================================
#include <gtest/gtest.h>
#include <Drv/LinuxGpioDriver/LinuxGpioDriver.hpp>
#include <cerrno>

namespace {

// Data ready is the only case that reads an event record.
TEST(GpioPoll, ReadyMeansReadTheEvent) {
    EXPECT_EQ(Drv::GpioPollAction::READ_EVENT, Drv::classify_poll_result(1, 0));
    // poll() reports the number of ready descriptors; any positive count is
    // the same decision here, since the loop polls exactly one fd.
    EXPECT_EQ(Drv::GpioPollAction::READ_EVENT, Drv::classify_poll_result(2, 0));
}

// A timeout is the normal idle path and must not be confused with an error.
TEST(GpioPoll, TimeoutIsIdleNotAnError) {
    EXPECT_EQ(Drv::GpioPollAction::IDLE, Drv::classify_poll_result(0, 0));
    // errno is meaningless when poll() did not fail, and must be ignored --
    // it holds whatever the last failing call left behind.
    EXPECT_EQ(Drv::GpioPollAction::IDLE, Drv::classify_poll_result(0, EBADF));
}

// The first of the two defects: EINTR is expected, not reportable.
TEST(GpioPoll, InterruptedPollRetriesWithoutReporting) {
    EXPECT_EQ(Drv::GpioPollAction::RETRY_QUIETLY, Drv::classify_poll_result(-1, EINTR));
}

// The second: everything else is a real error and must be paced, because
// poll() returns immediately when the descriptor is broken.
TEST(GpioPoll, RealErrorsAreReportedAndPaced) {
    EXPECT_EQ(Drv::GpioPollAction::REPORT_AND_BACK_OFF, Drv::classify_poll_result(-1, EBADF));
    EXPECT_EQ(Drv::GpioPollAction::REPORT_AND_BACK_OFF, Drv::classify_poll_result(-1, EINVAL));
    EXPECT_EQ(Drv::GpioPollAction::REPORT_AND_BACK_OFF, Drv::classify_poll_result(-1, ENOMEM));
    // An unrecognised errno is still an error; the decision must not depend
    // on enumerating every value poll() can set.
    EXPECT_EQ(Drv::GpioPollAction::REPORT_AND_BACK_OFF, Drv::classify_poll_result(-1, 0x7FFF));
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
