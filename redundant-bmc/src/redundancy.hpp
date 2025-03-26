// SPDX-License-Identifier: Apache-2.0
#pragma once

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
    bool siblingHasSiblingComm;
    Role siblingRole;
    BMCState siblingState;
    bool codeVersionsMatch;
    bool manualDisable;
    bool redundancyOffAtRuntimeStart;
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
    siblingNoCommunication,
    codeMismatch,
    siblingNotAtReady,
    systemHardwareConfigIssue,
    redundancyOffAtRuntimeStart,
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
} // namespace rbmc
