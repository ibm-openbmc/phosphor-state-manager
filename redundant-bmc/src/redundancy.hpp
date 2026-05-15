// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "services.hpp"

#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>
#include <xyz/openbmc_project/State/BMC/common.hpp>

namespace rbmc
{

using Role =
    sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy::Role;

using ReasonForNoRedundancy = sdbusplus::common::xyz::openbmc_project::state::
    bmc::Redundancy::ReasonForNoRedundancy;
using FailoversNotAllowedReason = sdbusplus::common::xyz::openbmc_project::
    state::bmc::Redundancy::FailoversNotAllowedReason;

using BMCState = sdbusplus::common::xyz::openbmc_project::state::BMC::BMCState;

namespace redundancy
{

/**
 * @brief Inputs to the getNoRedundancyReasons function
 */
struct Input
{
    Role role;
    bool siblingPresent;
    bool siblingAlive;
    bool siblingPaired;
    Role siblingRole;
    bool siblingCannotBeActive;
    BMCState siblingState;
    bool codeVersionsMatch;
    bool manualDisable;
    bool redundancyOffAtRuntimeStart;
    bool syncFailed;
    bool peerConnected;
};

using ReasonsForNoRedundancy = std::vector<ReasonForNoRedundancy>;

/**
 * @brief Returns the reasons that redundancy can't be enabled.
 *
 * @return The reasons.  Empty if it can be enabled.
 */
ReasonsForNoRedundancy getNoRedundancyReasons(const Input& input);

} // namespace redundancy

// Failovers not allowed
namespace fona
{

/**
 * @brief Inputs to the getFailoversNotAllowedReasons function
 */
struct Input
{
    // TODO: Add more
    bool redundancyEnabled;
    bool fullSyncComplete;
    bool failoverInProgress;
    SystemState systemState;
};

/**
 * @brief Returns the reason that failovers aren't allowed
 *
 * @return The reason.  None if there isn't any.
 */
FailoversNotAllowedReason getFailoversNotAllowedReason(const Input& input);

/**
 * @brief Return the string description of the reason
 *
 * @return The human readable description.
 */
std::string getFailoversNotAllowedDescription(FailoversNotAllowedReason reason);

} // namespace fona

namespace fo_blocked
{

/**
 * @brief Inputs to the is failover blocked  function
 */
struct Input
{
    bool siblingAlive;
    BMCState siblingState;
    bool redundancyEnabled;
    BMCState state;
    bool failoversNotAllowed;
    bool forceOption;
    bool failoverInProgress;
    bool lastKnownRedundancyEnabled;
};

/**
 * @brief Reasons why a failover is blocked
 */
enum class Reason
{
    none,
    redundancyNotEnabled,
    failoversNotAllowed,
    siblingDeadButRedundancyNotEnabled,
    notAtReady,
    bmcNotPassive,
    failoverAlreadyInProgress,
    tooEarly
};

/**
 * @brief Returns the reason a failover is blocked by the passive BMC
 *
 * @param[in] input - The current system states that will be checked.
 *
 * @return Reason::none if blocked, else the reason it is.
 */
Reason getFailoverBlockedReason(const Input& input);

/**
 * @brief Return the string description of the reason
 *
 * @return The human readable description.
 */
std::string getFailoverBlockedDescription(Reason reason);

} // namespace fo_blocked

} // namespace rbmc
