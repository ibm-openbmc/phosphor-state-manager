// SPDX-License-Identifier: Apache-2.0
#include "system_state.hpp"

#include <gtest/gtest.h>

using namespace rbmc;

using HostState =
    sdbusplus::common::xyz::openbmc_project::state::Host::HostState;
using BootProgress = sdbusplus::common::xyz::openbmc_project::state::boot::
    Progress::ProgressStages;

TEST(SystemStateTest, HostStateOff)
{
    // When host state is Off, system state should be off
    // regardless of boot progress
    auto result =
        calculateSystemState(HostState::Off, BootProgress::Unspecified);
    EXPECT_EQ(result, SystemState::off);

    result = calculateSystemState(HostState::Off, BootProgress::OSRunning);
    EXPECT_EQ(result, SystemState::off);

    result =
        calculateSystemState(HostState::Off, BootProgress::SystemInitComplete);
    EXPECT_EQ(result, SystemState::off);
}

TEST(SystemStateTest, HostStateTransitioningToRunning)
{
    // When host state is TransitioningToRunning, system state
    // should be booting regardless of boot progress
    auto result = calculateSystemState(HostState::TransitioningToRunning,
                                       BootProgress::Unspecified);
    EXPECT_EQ(result, SystemState::booting);

    result = calculateSystemState(HostState::TransitioningToRunning,
                                  BootProgress::OSRunning);
    EXPECT_EQ(result, SystemState::booting);

    result = calculateSystemState(HostState::TransitioningToRunning,
                                  BootProgress::SystemInitComplete);
    EXPECT_EQ(result, SystemState::booting);
}

TEST(SystemStateTest, HostStateRunningWithSystemInitComplete)
{
    // When host state is Running and boot progress is SystemInitComplete,
    // system state should be runtime
    auto result = calculateSystemState(HostState::Running,
                                       BootProgress::SystemInitComplete);
    EXPECT_EQ(result, SystemState::runtime);
}

TEST(SystemStateTest, HostStateRunningWithOSRunning)
{
    // When host state is Running and boot progress is OSRunning,
    // system state should be runtime
    auto result =
        calculateSystemState(HostState::Running, BootProgress::OSRunning);
    EXPECT_EQ(result, SystemState::runtime);
}

TEST(SystemStateTest, HostStateRunningWithOtherBootProgress)
{
    // When host state is Running but boot progress is not SystemInitComplete
    // or OSRunning, system state should be booting
    auto result =
        calculateSystemState(HostState::Running, BootProgress::Unspecified);
    EXPECT_EQ(result, SystemState::booting);

    result = calculateSystemState(HostState::Running, BootProgress::MemoryInit);
    EXPECT_EQ(result, SystemState::booting);

    result = calculateSystemState(HostState::Running, BootProgress::PCIInit);
    EXPECT_EQ(result, SystemState::booting);

    result =
        calculateSystemState(HostState::Running, BootProgress::PrimaryProcInit);
    EXPECT_EQ(result, SystemState::booting);

    result = calculateSystemState(HostState::Running,
                                  BootProgress::SecondaryProcInit);
    EXPECT_EQ(result, SystemState::booting);

    result = calculateSystemState(HostState::Running, BootProgress::OSStart);
    EXPECT_EQ(result, SystemState::booting);

    result =
        calculateSystemState(HostState::Running, BootProgress::MotherboardInit);
    EXPECT_EQ(result, SystemState::booting);
}

TEST(SystemStateTest, OtherHostStates)
{
    // For any other host state, system state should be other
    auto result =
        calculateSystemState(HostState::Quiesced, BootProgress::Unspecified);
    EXPECT_EQ(result, SystemState::other);

    result = calculateSystemState(HostState::DiagnosticMode,
                                  BootProgress::Unspecified);
    EXPECT_EQ(result, SystemState::other);

    result = calculateSystemState(HostState::TransitioningToOff,
                                  BootProgress::Unspecified);
    EXPECT_EQ(result, SystemState::other);
}

TEST(SystemStateTest, GetSystemStateName)
{
    EXPECT_EQ(getSystemStateName(SystemState::off), "Off");
    EXPECT_EQ(getSystemStateName(SystemState::booting), "Booting");
    EXPECT_EQ(getSystemStateName(SystemState::runtime), "Runtime");
    EXPECT_EQ(getSystemStateName(SystemState::other), "Other");
}
