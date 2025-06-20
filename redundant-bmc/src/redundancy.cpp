// SPDX-License-Identifier: Apache-2.0
#include "redundancy.hpp"

#include <phosphor-logging/lg2.hpp>

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

    if (!input.fullSyncComplete)
    {
        reasons.insert(fullSyncNotComplete);
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
        case fullSyncNotComplete:
            desc = "A full sync hasn't been completed"s;
            break;
        case redundancyDisabled:
            desc = "Redundancy is disabled"s;
            break;
    }
    return desc;
}

} // namespace fona

namespace fo_blocked
{

Reason getFailoverBlockedReason(const Input& input)
{
    if (input.siblingHeartbeat)
    {
        if (!input.redundancyEnabled)
        {
            return Reason::redundancyNotEnabled;
        }
        else if (input.failoversNotAllowed)
        {
            // Don't block a failover even if the failover is not allowed when:
            //  1. the force option was given on the start failover cmd, or
            //  2. the active BMC is Quiesced.
            if (input.forceOption)
            {
                // Trace it but don't block it.
                lg2::info(
                    "The failover 'Force' option is set while failovers are not allowed");
            }
            else if (input.siblingState == BMCState::Quiesced)
            {
                // If the active BMC is quiesced, it may be stuck in
                // failovers-not-allowed so don't block it, just trace it.
                lg2::info(
                    "The sibling BMC is quiesced while failovers are not allowed");
            }
            else
            {
                return Reason::failoversNotAllowed;
            }
        }
        else if (input.syncInProgress)
        {
            // The passive BMC is in the middle of its full sync
            return Reason::fullSyncInProgress;
        }
    }
    else
    {
        // The active BMC isn't responding.  Use its last known value of
        // RedundancyEnabled to decide if a failover is OK.  Not perfect, but
        // otherwise we could be stuck with 1 dead BMC and 1 passive BMC with no
        // way to fail over.
        if (!input.lastKnownRedundancyEnabled)
        {
            return Reason::siblingDeadButRedundancyNotEnabled;
        }

        lg2::info(
            "There is no sibling heartbeat but redundancy was last known to be enabled");

        if (input.failoversNotAllowed)
        {
            // Still allow the failover in this case because the value could
            // have been latched by the active BMC before it died.
            lg2::info("In addition, failovers were previously not allowed");
        }
    }

    // If this BMC is not at Ready, that needs to be fixed first
    // before it can fail over to active.  Normally redundancy would
    // have been disabled in this case if active BMC is alive.
    if (input.state != BMCState::Ready)
    {
        return Reason::notAtReady;
    }

    return Reason::none;
}

std::string getFailoverBlockedDescription(Reason reason)
{
    using namespace std::string_literals;
    std::string desc;

    switch (reason)
    {
        case Reason::none:
            desc = "No reason"s;
            break;
        case Reason::redundancyNotEnabled:
            desc = "Redundancy is not enabled"s;
            break;
        case Reason::fullSyncInProgress:
            desc = "Full sync is in progress"s;
            break;
        case Reason::failoversNotAllowed:
            desc = "Failovers are not allowed"s;
            break;
        case Reason::siblingDeadButRedundancyNotEnabled:
            desc = "Sibling is dead but redundancy wasn't previously enabled"s;
            break;
        case Reason::notAtReady:
            desc = "This BMC is not at Ready state"s;
            break;
    }
    return desc;
}

} // namespace fo_blocked

} // namespace rbmc
