// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pcie_storage.hpp"

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
     *
     * @param[in] pcieStorage - Pointer to the PCIeStorage class if configured
     */
    RedundancyInterface(sdbusplus::async::context& ctx, Manager& manager,
                        pcie_data::PCIeStorage* pcieStorage);

    /**
     * @brief Implements property set for the
     *        Role property
     *
     * @param[in] role_t - The type
     * @param[in] role - the value being set
     *
     * @return If the property value changed
     */
    bool set_property(role_t type, Role role);

    /**
     * @brief Implements property set for the
     *        RedundancyEnabled property
     *
     * @param[in] redundancy_enabled_t - The type
     * @param[in] enabled - the value being set
     *
     * @return If the property value changed
     */
    bool set_property(redundancy_enabled_t type, bool enabled);

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

    /**
     * @brief Implements the SetRedundancyInput D-Bus method
     *
     * Sets an external redundancy input checked when enabling redundancy
     *
     * @param[in] input - The RedundancyInput to set
     * @param[in] value - The value to set it to
     */
    sdbusplus::async::task<> method_call(set_redundancy_input_t /* unused */,
                                         RedundancyInput input, bool value);

    /**
     * @brief Implements property set for the
     *        FailoversAllowed property
     *
     * @param[in] failovers_allowed_t - The type
     * @param[in] allowed - the value being set
     *
     * @return If the property value changed
     */
    bool set_property(failovers_allowed_t type, bool allowed);

  private:
    /**
     * @brief Reference to the Manager class
     */
    Manager& manager;

    /**
     * @brief Pointer to the PCIeStorage class if configured
     */
    pcie_data::PCIeStorage* pcieStorage;
};

} // namespace rbmc
