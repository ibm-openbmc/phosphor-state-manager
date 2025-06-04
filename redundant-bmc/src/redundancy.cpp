// SPDX-License-Identifier: Apache-2.0
#include "redundancy.hpp"

namespace rbmc
{
namespace redundancy
{

NoRedundancyReasons getNoRedundancyReasons(const Input& input)
{
    using enum NoRedundancyReason;
    NoRedundancyReasons reasons;

    // TODO:
    // - Network and/or sync health
    // - Can't enable redundancy if system wasn't booted with it enabled

    if (input.role != Role::Active)
    {
        reasons.insert(bmcNotActive);
    }

    if (input.manualDisable)
    {
        reasons.insert(manuallyDisabled);
    }

    if (!input.siblingPresent)
    {
        reasons.insert(siblingMissing);
    }
    else
    {
        if (!input.siblingHeartbeat)
        {
            reasons.insert(noSiblingHeartbeat);
        }
        else
        {
            if (!input.siblingProvisioned)
            {
                reasons.insert(siblingNotProvisioned);
            }

            if (input.siblingRole != Role::Passive)
            {
                reasons.insert(siblingNotPassive);
            }

            if (!input.codeVersionsMatch)
            {
                reasons.insert(codeMismatch);
            }

            if (input.siblingState != BMCState::Ready)
            {
                reasons.insert(siblingNotAtReady);
            }

            if (input.syncFailed)
            {
                reasons.insert(syncFailed);
            }
        }
    }

    if (input.redundancyOffAtRuntimeStart)
    {
        reasons.insert(redundancyOffAtRuntimeStart);
    }

    return reasons;
}

std::string getNoRedundancyDescription(NoRedundancyReason reason)
{
    using enum NoRedundancyReason;
    using namespace std::string_literals;
    std::string desc;

    // Use a switch so that the compiler will catch missing enums.
    switch (reason)
    {
        case none:
            desc = "None"s;
            break;
        case bmcNotActive:
            desc = "BMC is not active"s;
            break;
        case manuallyDisabled:
            desc = "Manually disabled"s;
            break;
        case siblingMissing:
            desc = "Sibling is missing"s;
            break;
        case noSiblingHeartbeat:
            desc = "No sibling heartbeat"s;
            break;
        case siblingNotProvisioned:
            desc = "Sibling is not provisioned"s;
            break;
        case siblingNotPassive:
            desc = "Sibling is not passive"s;
            break;
        case codeMismatch:
            desc = "Firmware version mismatch"s;
            break;
        case siblingNotAtReady:
            desc = "Sibling is not at ready state"s;
            break;
        case systemHardwareConfigIssue:
            desc = "System hardware configuration issue"s;
            break;
        case redundancyOffAtRuntimeStart:
            desc = "Redundancy was off upon reaching runtime"s;
            break;
        case syncFailed:
            desc = "Data sync failed"s;
            break;
        case other:
            desc = "Other";
            break;
    }
    return desc;
}

} // namespace redundancy

namespace fona
{

FailoversNotAllowedReasons getFailoversNotAllowedReasons(const Input& input)
{
    using enum FailoversNotAllowedReason;
    FailoversNotAllowedReasons reasons;

    if (!input.redundancyEnabled)
    {
        reasons.insert(redundancyDisabled);
        // No need to look for more reasons
        return reasons;
    }

    if ((input.systemState != SystemState::off) &&
        (input.systemState != SystemState::runtime))
    {
        reasons.insert(systemState);
    }

    return reasons;
}

std::string getFailoversNotAllowedDescription(FailoversNotAllowedReason reason)
{
    using enum FailoversNotAllowedReason;
    using namespace std::string_literals;
    std::string desc;

    switch (reason)
    {
        case systemState:
            desc = "System state is not off or runtime"s;
            break;
        case redundancyDisabled:
            desc = "Redundancy is disabled"s;
            break;
    }
    return desc;
}

} // namespace fona
} // namespace rbmc
