// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "services.hpp"

#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>
#include <xyz/openbmc_project/State/BMC/common.hpp>

namespace rbmc
{

using Role =
    sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy::Role;

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
    bool siblingHeartbeat;
    bool siblingProvisioned;
    Role siblingRole;
    BMCState siblingState;
    bool codeVersionsMatch;
    bool manualDisable;
    bool redundancyOffAtRuntimeStart;
    bool syncFailed;
};

// TODO: Move this to PDI Enums
/**
 * @brief Reasons why redundancy can't be enabled
 */
enum class NoRedundancyReason
{
    none,
    bmcNotActive,
    manuallyDisabled,
    siblingMissing,
    noSiblingHeartbeat,
    siblingNotProvisioned,
    siblingNotPassive,
    codeMismatch,
    siblingNotAtReady,
    systemHardwareConfigIssue,
    redundancyOffAtRuntimeStart,
    syncFailed,
    other
};

using NoRedundancyReasons = std::set<NoRedundancyReason>;

/**
 * @brief Returns the reasons that redundancy can't be enabled.
 *
 * @return The reasons.  Empty if it can be enabled.
 */
NoRedundancyReasons getNoRedundancyReasons(const Input& input);

/**
 * @brief Return the string description of the reason
 *
 * @return The human readable description.
 */
std::string getNoRedundancyDescription(NoRedundancyReason reason);

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
    SystemState systemState;
};

/**
 * @brief Reasons why failovers aren't allowed
 */
enum class FailoversNotAllowedReason
{
    redundancyDisabled,
    fullSyncNotComplete,
    systemState
};

using FailoversNotAllowedReasons = std::set<FailoversNotAllowedReason>;

/**
 * @brief Returns the reasons that failovers aren't allowed
 *
 * @return The reasons.  Empty if there are none.
 */
FailoversNotAllowedReasons getFailoversNotAllowedReasons(const Input& input);

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
    bool siblingHeartbeat;
    BMCState siblingState;
    bool redundancyEnabled;
    bool syncInProgress;
    BMCState state;
    bool failoversNotAllowed;
    bool forceOption;
    bool lastKnownRedundancyEnabled;
};

/**
 * @brief Reasons why a failover is blocked
 */
enum class Reason
{
    none,
    redundancyNotEnabled,
    fullSyncInProgress,
    failoversNotAllowed,
    siblingDeadButRedundancyNotEnabled,
    notAtReady,
    bmcNotPassive
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
