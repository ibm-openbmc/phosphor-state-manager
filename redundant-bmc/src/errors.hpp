// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#pragma once

#include <xyz/openbmc_project/Logging/Entry/common.hpp>

#include <map>
#include <string>

namespace rbmc::errors
{

using Level = sdbusplus::common::xyz::openbmc_project::logging::Entry::Level;

using AdditionalData = std::map<std::string, std::string>;

namespace error_msg
{
const std::string failoverStarted =
    "xyz.openbmc_project.State.BMC.Redundancy.FailoverStarted";

const std::string failoverBlocked =
    "xyz.openbmc_project.State.BMC.Redundancy.FailoverBlocked";

const std::string redundancyManuallyDisabled =
    "xyz.openbmc_project.State.BMC.Redundancy.RedundancyManuallyDisabled";

const std::string bmcIsPassiveDueToError =
    "xyz.openbmc_project.State.BMC.Redundancy.PassiveDueToError";

const std::string noRedundancy =
    "xyz.openbmc_project.State.BMC.Redundancy.NoRedundancy";

} // namespace error_msg

} // namespace rbmc::errors
