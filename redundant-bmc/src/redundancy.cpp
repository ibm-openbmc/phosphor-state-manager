// SPDX-License-Identifier: Apache-2.0
#include "redundancy.hpp"

#include <phosphor-logging/lg2.hpp>

namespace rbmc
{
namespace redundancy
{

ReasonsForNoRedundancy getNoRedundancyReasons(const Input& input)
{
    using enum ReasonForNoRedundancy;
    ReasonsForNoRedundancy reasons;

    if (input.role != Role::Active)
    {
        reasons.insert(BMCNotActive);
    }

    if (input.manualDisable)
    {
        reasons.insert(ManuallyDisabled);
    }

    if (!input.siblingPresent)
    {
        reasons.insert(SiblingMissing);
    }
    else
    {
        if (!input.siblingAlive)
        {
            reasons.insert(SiblingNotAlive);
        }
        else
        {
            if (!input.siblingProvisioned)
            {
                reasons.insert(SiblingNotProvisioned);
            }

            if (input.siblingRole != Role::Passive)
            {
                reasons.insert(SiblingNotPassive);
            }

            if (input.siblingCannotBeActive)
            {
                reasons.insert(SiblingCannotBeActive);
            }

            if (!input.codeVersionsMatch)
            {
                reasons.insert(CodeVersionMismatch);
            }

            if (!input.peerConnected)
            {
                reasons.insert(NetworkError);
            }

            if (input.siblingState != BMCState::Ready)
            {
                reasons.insert(SiblingNotAtReady);
            }

            if (input.syncFailed)
            {
                reasons.insert(DataSyncFailed);
            }
        }
    }

    if (input.redundancyOffAtRuntimeStart)
    {
        reasons.insert(RedundancyOffAtRuntimeStart);
    }

    return reasons;
}

} // namespace redundancy

namespace fona
{

FailoversNotAllowedReason getFailoversNotAllowedReason(const Input& input)
{
    using enum FailoversNotAllowedReason;

    if (!input.redundancyEnabled)
    {
        return NoRedundancy;
    }

    if (input.failoverInProgress)
    {
        return FailoverInProgress;
    }

    if (!input.fullSyncComplete)
    {
        return FullSyncNotComplete;
    }

    if ((input.systemState != SystemState::off) &&
        (input.systemState != SystemState::runtime))
    {
        return WrongSystemState;
    }

    return None;
}

std::string getFailoversNotAllowedDescription(FailoversNotAllowedReason reason)
{
    using enum FailoversNotAllowedReason;
    using namespace std::string_literals;
    std::string desc;

    switch (reason)
    {
        case WrongSystemState:
            desc = "System state is not off or runtime"s;
            break;
        case FullSyncNotComplete:
            desc = "A full sync hasn't been completed"s;
            break;
        case FailoverInProgress:
            desc = "A failover is in progress"s;
            break;
        case NoRedundancy:
            desc = "Redundancy is disabled"s;
            break;
        case None:
            desc = "No reason"s;
            break;
    }
    return desc;
}

} // namespace fona

namespace fo_blocked
{

Reason getFailoverBlockedReason(const Input& input)
{
    if (input.siblingAlive)
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
        else if (input.failoverInProgress)
        {
            return Reason::failoverAlreadyInProgress;
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
            "Sibling not alive but redundancy was last known to be enabled");

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
        case Reason::failoversNotAllowed:
            desc = "Failovers are not allowed"s;
            break;
        case Reason::siblingDeadButRedundancyNotEnabled:
            desc = "Sibling is dead but redundancy wasn't previously enabled"s;
            break;
        case Reason::notAtReady:
            desc = "This BMC is not at Ready state"s;
            break;
        case Reason::bmcNotPassive:
            desc = "This BMC is not passive"s;
            break;
        case Reason::failoverAlreadyInProgress:
            desc = "A failover is already in progress"s;
            break;
        case Reason::tooEarly:
            desc = "It is too early in the BMC boot to start a failover"s;
    }
    return desc;
}

} // namespace fo_blocked

} // namespace rbmc
