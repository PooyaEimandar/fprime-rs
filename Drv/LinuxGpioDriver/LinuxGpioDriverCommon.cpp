// ======================================================================
// \title  LinuxGpioDriverImpl.cpp
// \author tcanham
// \brief  cpp file for LinuxGpioDriver component implementation class
//
// \copyright
// Copyright 2009-2015, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#include <Drv/LinuxGpioDriver/LinuxGpioDriver.hpp>
#include <Fw/FPrimeBasicTypes.hpp>
#include <cerrno>

namespace Drv {

GpioPollAction classify_poll_result(int pollStatus, int pollErrno) {
    if (pollStatus > 0) {
        return GpioPollAction::READ_EVENT;
    }
    if (pollStatus == 0) {
        return GpioPollAction::IDLE;
    }
    // EINTR is a signal arriving during poll(), not a fault: the previous
    // implementation reported it as WARNING_HI on every occurrence, which on a
    // signal-heavy system is a flood of events describing normal operation.
    if (pollErrno == EINTR) {
        return GpioPollAction::RETRY_QUIETLY;
    }
    // Anything else is a real error, and the reason this classification exists.
    // poll() returns immediately on a persistent failure -- a closed or invalid
    // fd, for instance -- so a loop that only logged and continued span at the
    // speed of the CPU, emitting one WARNING_HI per iteration. The caller paces
    // itself before retrying.
    return GpioPollAction::REPORT_AND_BACK_OFF;
}


// ----------------------------------------------------------------------
// Construction, initialization, and destruction
// ----------------------------------------------------------------------

LinuxGpioDriver ::LinuxGpioDriver(const char* const compName) : LinuxGpioDriverComponentBase(compName) {}

Drv::GpioStatus LinuxGpioDriver ::start(const FwTaskPriorityType priority,
                                        const FwSizeType stackSize,
                                        const FwSizeType cpuAffinity,
                                        const FwTaskIdType identifier) {
    Drv::GpioStatus status = Drv::GpioStatus::INVALID_MODE;
    if (this->m_configuration < GpioConfiguration::MAX_GPIO_CONFIGURATION &&
        this->m_configuration >= GpioConfiguration::GPIO_INTERRUPT_RISING_EDGE) {
        status = Drv::GpioStatus::OP_OK;
        {
            Os::ScopeLock lock(m_lock);
            this->m_running = true;
        }
        Fw::String name;
        (void)name.format("%s.interrupt", FW_OPTIONAL_NAME(this->getObjName()));  // task name may safely truncate
        Os::Task::Arguments arguments(name, &this->interruptFunction, this, priority, stackSize, cpuAffinity,
                                      identifier);
        Os::Task::Status taskStatus = this->m_poller.start(arguments);
        if (taskStatus != Os::Task::Status::OP_OK) {
            status = Drv::GpioStatus::UNKNOWN_ERROR;
        }
    }
    return status;
}

void LinuxGpioDriver ::stop() {
    Os::ScopeLock lock(m_lock);
    this->m_running = false;
}

void LinuxGpioDriver ::join() {
    (void)this->m_poller.join();  // best-effort join on shutdown
}

void LinuxGpioDriver ::interruptFunction(void* self) {
    FW_ASSERT(self != nullptr);
    LinuxGpioDriver* component = reinterpret_cast<LinuxGpioDriver*>(self);
    component->pollLoop();
}

bool LinuxGpioDriver ::getRunning() {
    Os::ScopeLock lock(m_lock);
    return this->m_running;
}

}  // end namespace Drv
