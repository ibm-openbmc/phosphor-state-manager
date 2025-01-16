// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "redundancy.hpp"
#include "redundancy_interface.hpp"
#include "services.hpp"
#include "sibling.hpp"

namespace rbmc
{

/**
 * @class RedundancyMgr
 *
 * This class manages the redundancy related functionality.
 */
class RedundancyMgr
{
  public:
    ~RedundancyMgr() = default;
    RedundancyMgr(const RedundancyMgr&) = delete;
    RedundancyMgr& operator=(const RedundancyMgr&) = delete;
    RedundancyMgr(RedundancyMgr&&) = delete;
    RedundancyMgr& operator=(RedundancyMgr&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     * @param[in] services - The services object
     * @param[in] sibling - The sibling object
     * @param[in] iface - The redundancy D-Bus interface object
     */
    RedundancyMgr(sdbusplus::async::context& ctx, Services& services,
                  Sibling& sibling, RedundancyInterface& iface);

    /**
     * @brief Enables or disables redundancy based on the
     *        system config.
     */
    void determineAndSetRedundancy();

    /**
     * @brief Called when the DisableRedundancyOverride D-Bus property
     *        is updated.
     *
     * May disable or enable redundancy at this time if possible.
     *
     * @param[in] disable - If redundancy should be disabled
     *                      or enabled.
     */
    void disableRedPropChanged(bool disable);

  private:
    /**
     * @brief Returns the reasons that redundancy cannnot be
     *        enabled, if any.
     *
     * @return The reasons.  If empty, then redundancy can be enabled.
     */
    redundancy::NoRedundancyReasons getNoRedundancyReasons();

    /**
     * @brief Based on if disableReasons is empty or not, disables
     *        or enables redundancy.
     *
     * @param[in] disableReasons - The reasons redundancy must be
     *            disabled, if any.
     */
    void enableOrDisableRedundancy(
        const redundancy::NoRedundancyReasons& disableReasons);

    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;

    /**
     * @brief The services object
     */
    Services& services;

    /**
     * @brief The Sibling object
     */
    Sibling& sibling;

    /**
     * @brief The Redundancy D-Bus interface
     */
    RedundancyInterface& redundancyInterface;

    /**
     * @brief If determineAndSetRedundancy() has ran yet or not.
     */
    bool redundancyDetermined = false;

    /**
     * @brief If redundancy has been manually disabled via the
     *        D-Bus property.
     */
    bool manualDisable = false;
};

} // namespace rbmc
