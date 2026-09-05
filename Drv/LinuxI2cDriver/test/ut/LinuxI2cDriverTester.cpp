// ======================================================================
// \title  LinuxI2cDriver.hpp
// \author tcanham
// \brief  cpp file for LinuxI2cDriver test harness implementation class
//
// \copyright
// Copyright 2009-2015, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#include "LinuxI2cDriverTester.hpp"

#define INSTANCE 0
#define MAX_HISTORY_SIZE 10

namespace Drv {

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

LinuxI2cDriverTester ::LinuxI2cDriverTester()
    : LinuxI2cDriverGTestBase("Tester", MAX_HISTORY_SIZE), component("LinuxI2cDriver") {
    this->initComponents();
    this->connectPorts();
}

LinuxI2cDriverTester ::~LinuxI2cDriverTester() {}

// ----------------------------------------------------------------------
// Tests
//
// NOTE: no CMakeLists.txt registers this harness, so nothing builds or runs
// it. That is deliberate rather than an oversight -- main.cpp takes a
// `-d <device>` argument and drives a real /dev/i2c-* node, which CI does not
// have. It went stale while unbuilt: it called Fw::Buffer::setdata/setsize,
// renamed to setData/setSize, and so had not compiled for some time. The two
// calls are corrected, but the harness is still hardware-only. Anything that
// should run in CI belongs in a separate test of the reachable error paths
// (a closed fd yields I2C_OPEN_ERR; ioctl(I2C_SLAVE) on a non-i2c fd yields
// I2C_ADDRESS_ERR), which does not exist yet.
// ----------------------------------------------------------------------

void LinuxI2cDriverTester ::sendData(U32 addr, U8* data, Fw::Buffer::SizeType size) {
    Fw::Buffer dataBuff;
    dataBuff.setData(data);
    dataBuff.setSize(size);
    this->invoke_to_write(0, addr, dataBuff);
}

void LinuxI2cDriverTester::open(const char* device) {
    this->component.open(device);
}

// ----------------------------------------------------------------------
// Helper methods
// ----------------------------------------------------------------------

void LinuxI2cDriverTester ::connectPorts() {
    // write
    this->connect_to_write(0, this->component.get_write_InputPort(0));
}

void LinuxI2cDriverTester ::initComponents() {
    this->init();
    this->component.init(INSTANCE);
}

}  // end namespace Drv
