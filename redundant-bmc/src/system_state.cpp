/* SPDX-License-Identifier: Apache-2.0 */
#include "system_state.hpp"

namespace rbmc
{

using HostState =
    sdbusplus::common::xyz::openbmc_project::state::Host::HostState;
using BootProgress = sdbusplus::common::xyz::openbmc_project::state::boot::
    Progress::ProgressStages;

SystemState calculateSystemState(HostState hostState, BootProgress bootProgress)
{
    SystemState state = SystemState::other;

    if (hostState == HostState::Off)
    {
        state = SystemState::off;
    }
    else if (hostState == HostState::TransitioningToRunning)
    {
        state = SystemState::booting;
    }
    else if (hostState == HostState::Running)
    {
        if ((bootProgress == BootProgress::SystemInitComplete) ||
            (bootProgress == BootProgress::OSRunning))
        {
            state = SystemState::runtime;
        }
        else
        {
            state = SystemState::booting;
        }
    }

    return state;
}

} // namespace rbmc
