// ======================================================================
// \title  LinuxUartDriverTestMain.cpp
// \brief  Characterization tests for LinuxUartDriver
//
// This driver had no tests at all. Per the fprime-rs plan, characterization
// tests come BEFORE any rewrite, so the behavior being preserved is written
// down first and any deliberate change is visible as a test change.
//
// Two harnesses, because the driver's two interesting behaviors need
// different ones:
//
//   * A pseudo-terminal pair stands in for a real serial port. The driver
//     opens the slave by path exactly as it would /dev/ttyUSB0, so open(),
//     configuration, and the write path run unmodified while the test drives
//     and observes the other end.
//   * A deliberately-filled non-blocking pipe forces the short-write path,
//     which a pty cannot do reliably: writes to a blocking tty either
//     complete or block. This is the path where the driver used to lose the
//     unwritten remainder of a buffer.
// ======================================================================

#include <gtest/gtest.h>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

#include <cstring>
#include <string>

#include "Drv/LinuxUartDriver/LinuxUartDriver.hpp"

namespace {

//! A pseudo-terminal pair: `master` is the test's end, `slavePath` is what the
//! driver opens.
class PtyPair {
  public:
    PtyPair() : master(-1), slave(-1) {
        char name[256] = {0};
        const int status = ::openpty(&this->master, &this->slave, name, nullptr, nullptr);
        if (status == 0) {
            this->slavePath = name;
            // The driver opens the slave itself; close our copy so the only
            // reference is the driver's.
            ::close(this->slave);
            this->slave = -1;
        }
    }

    ~PtyPair() {
        if (this->master >= 0) {
            ::close(this->master);
        }
        if (this->slave >= 0) {
            ::close(this->slave);
        }
    }

    bool valid() const { return this->master >= 0 && !this->slavePath.empty(); }

    int master;
    int slave;
    std::string slavePath;
};

}  // namespace

// ----------------------------------------------------------------------
// writeAll: the short-write path, pinned with a full non-blocking pipe
// ----------------------------------------------------------------------

TEST(LinuxUartDriverWriteAll, WritesEveryByteOfALargeBuffer) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(fds), 0);

    // Drain concurrently is unnecessary: write a payload that fits the pipe
    U8 payload[512];
    for (size_t i = 0; i < sizeof payload; i++) {
        payload[i] = static_cast<U8>(i & 0xFF);
    }

    FwSizeType written = 0;
    ASSERT_TRUE(Drv::LinuxUartDriver::writeAll(fds[1], payload, sizeof payload, written));
    ASSERT_EQ(written, static_cast<FwSizeType>(sizeof payload));

    U8 readback[sizeof payload] = {0};
    ASSERT_EQ(::read(fds[0], readback, sizeof readback), static_cast<ssize_t>(sizeof payload));
    ASSERT_EQ(0, memcmp(payload, readback, sizeof payload));

    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(LinuxUartDriverWriteAll, ReportsBytesWrittenWhenTheDeviceStopsAccepting) {
    // A non-blocking pipe that is already full accepts a prefix and then
    // refuses the rest. The old send path reported this as a plain error and
    // said nothing about how much had gone out; the caller could neither
    // resend safely nor account for the loss.
    int fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(fds), 0);
    ASSERT_EQ(::fcntl(fds[1], F_SETFL, O_NONBLOCK), 0);

    // Fill the pipe so only a prefix of the next write can be accepted
    U8 filler[4096];
    memset(filler, 0xAA, sizeof filler);
    ssize_t accepted = 0;
    do {
        accepted = ::write(fds[1], filler, sizeof filler);
    } while (accepted > 0);

    // Free a little room, then attempt a much larger write
    U8 drain[256] = {0};
    ASSERT_GT(::read(fds[0], drain, sizeof drain), 0);

    U8 payload[8192];
    memset(payload, 0x5A, sizeof payload);
    FwSizeType written = 0;
    const bool complete = Drv::LinuxUartDriver::writeAll(fds[1], payload, sizeof payload, written);

    ASSERT_FALSE(complete) << "a full pipe cannot accept the whole payload";
    ASSERT_GT(written, static_cast<FwSizeType>(0)) << "the prefix that was accepted must be reported";
    ASSERT_LT(written, static_cast<FwSizeType>(sizeof payload));

    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(LinuxUartDriverWriteAll, ReportsZeroWrittenOnAClosedDescriptor) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(::pipe(fds), 0);
    ::close(fds[0]);
    ::close(fds[1]);

    const U8 payload[4] = {1, 2, 3, 4};
    FwSizeType written = 123;  // must be overwritten
    ASSERT_FALSE(Drv::LinuxUartDriver::writeAll(fds[1], payload, sizeof payload, written));
    ASSERT_EQ(written, static_cast<FwSizeType>(0));
}

// ----------------------------------------------------------------------
// open(): behavior against a real tty and against a missing device
// ----------------------------------------------------------------------

TEST(LinuxUartDriverOpen, SucceedsOnAPseudoTerminal) {
    PtyPair pty;
    ASSERT_TRUE(pty.valid()) << "could not allocate a pseudo-terminal";

    Drv::LinuxUartDriver driver("uart");
    ASSERT_TRUE(driver.open(pty.slavePath.c_str(), Drv::LinuxUartDriver::BAUD_9600,
                            Drv::LinuxUartDriver::NO_FLOW, Drv::LinuxUartDriver::PARITY_NONE, 1024));
}

TEST(LinuxUartDriverOpen, FailsOnAMissingDevice) {
    Drv::LinuxUartDriver driver("uart");
    ASSERT_FALSE(driver.open("/dev/definitely-not-a-real-tty", Drv::LinuxUartDriver::BAUD_9600,
                             Drv::LinuxUartDriver::NO_FLOW, Drv::LinuxUartDriver::PARITY_NONE, 1024));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
