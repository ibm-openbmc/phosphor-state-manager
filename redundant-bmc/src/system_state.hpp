// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <xyz/openbmc_project/State/Boot/Progress/common.hpp>
#include <xyz/openbmc_project/State/Host/common.hpp>

#include <string>

namespace rbmc
{

enum class SystemState
{
    off,
    booting,
    runtime,
    other
};

/**
 * @brief Calculate the system state based on host state and boot progress
 *
 * @param[in] hostState - The current host state
 * @param[in] bootProgress - The current boot progress stage
 *
 * @return The calculated system state
 */
SystemState calculateSystemState(
    sdbusplus::common::xyz::openbmc_project::state::Host::HostState hostState,
    sdbusplus::common::xyz::openbmc_project::state::boot::Progress::
        ProgressStages bootProgress);

/**
 * @brief Returns the string name for the system state enum
 *
 * @param[in] state - The state enum
 *
 * @return The string name
 */
inline std::string getSystemStateName(SystemState state)
{
    switch (state)
    {
        case SystemState::off:
            return "Off";
        case SystemState::booting:
            return "Booting";
        case SystemState::runtime:
            return "Runtime";
        case SystemState::other:
            return "Other";
    }

    return "Unknown";
}

} // namespace rbmc
