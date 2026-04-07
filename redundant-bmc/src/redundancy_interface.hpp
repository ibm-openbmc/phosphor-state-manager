// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/aserver.hpp>

namespace rbmc
{

class Manager;

/**
 * @class RedundancyInterface
 *
 * Has the necessary overrides for the Redundancy D-Bus interface
 */
class RedundancyInterface :
    public sdbusplus::aserver::xyz::openbmc_project::state::bmc::Redundancy<
        RedundancyInterface>
{
  public:
    RedundancyInterface(const RedundancyInterface&) = delete;
    RedundancyInterface& operator=(const RedundancyInterface&) = delete;
    RedundancyInterface(RedundancyInterface&&) = delete;
    RedundancyInterface& operator=(RedundancyInterface&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     *
     * @param[in] manager - Reference to the Manager class
     */
    RedundancyInterface(sdbusplus::async::context& ctx, Manager& manager);

    /**
     * @brief Implements property set for the
     *        DisableRedundancyOverride property
     *
     * @param[in] disable_redundancy_override_t - The type
     * @param[in] disable - the value being set
     *
     * @return If the property value changed
     */
    bool set_property(disable_redundancy_override_t type, bool disable);

    /**
     * @brief Implements property set for the
     *        FailoverInProgress property
     *
     * @param[in] failover_in_progress_t - The type
     * @param[in] inProgress - the value being set
     *
     * @return If the property value changed
     */
    bool set_property(failover_in_progress_t type, bool inProgress);

    /**
     * @brief Implements property set for the
     *        NoRedundancyReasons property
     *
     * @param[in] reasons_for_no_redundancy_t - The type
     * @param[in] reasons - the value being set
     *
     * @return If the property value changed
     */
    bool set_property(reasons_for_no_redundancy_t type,
                      const std::vector<ReasonForNoRedundancy>& reasons);

  private:
    /**
     * @brief Reference to the Manager class
     */
    Manager& manager;
};

} // namespace rbmc
