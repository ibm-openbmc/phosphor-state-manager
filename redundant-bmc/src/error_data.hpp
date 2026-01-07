// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "errors.hpp"
#include "providers.hpp"
#include "redundancy_interface.hpp"

namespace rbmc::errors
{

/**
 * @brief Adds the default set of redundancy related data to the
 *        AdditionalData map passed in.
 *
 * @param[in] iface - The RedundancyInterface object, for collecting data
 * @param[in] providers - The Providers object, for collecting data
 * @param[inout] data - Map to add the data to
 */
void addDefaultData(const RedundancyInterface& iface, Providers& providers,
                    AdditionalData& data);

} // namespace rbmc::errors
