// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "providers.hpp"
#include "redundancy.hpp"
#include "redundancy_interface.hpp"

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
    RedundancyMgr(const RedundancyMgr&) = delete;
    RedundancyMgr& operator=(const RedundancyMgr&) = delete;
    RedundancyMgr(RedundancyMgr&&) = delete;
    RedundancyMgr& operator=(RedundancyMgr&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     * @param[in] providers - The Providers access object
     * @param[in] iface - The redundancy D-Bus interface object
     */
    RedundancyMgr(sdbusplus::async::context& ctx, Providers& providers,
                  RedundancyInterface& iface);

    /**
     * @brief Destructor
     */
    ~RedundancyMgr()
    {
        providers.getServices().clearSystemStateCallbacks();
    }

    /**
     * @brief Enables or disables redundancy based on the
     *        system config.
     */
    void determineAndSetRedundancy();

    /**
     * @brief Determines redundancy, and if enabled does a full sync
     *
     * If the full sync fails, redundancy will be disabled.
     */
    sdbusplus::async::task<> determineRedundancyAndSync();

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

    /**
     * @brief Sets the FailoversPaused property based on the current
     *        state of the system.
     */
    void determineAndSetFailoversPaused();

    /**
     * @brief Disables redundancy due to a failed sync.
     */
    void handleBackgroundSyncFailed();

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
     * @brief Called when the system state changes to do
     *        any necessary work.
     *
     * @param[in] newState - the new system state
     */
    void systemStateChange(SystemState newState);

    /**
     * @brief Reads the initial system state value and registers
     *        for callbacks on changes
     */
    void initSystemState();

    /**
     * @brief Update the 'redundancy off at runtime' data.
     *
     * @param[in] valid - If the value is valid or not.
     * @param[in] off - The value, if redundancy is off or not.
     */
    static void setRedundancyOffAtRuntime(bool valid, bool off);

    /**
     * @brief Returns the 'redundancy off at runtime' values
     *
     * @return If value is valid, and what the value is
     */
    static std::tuple<bool, bool> getRedundancyOffAtRuntime();

    /**
     * @brief Says if Redundancy was off at runtime
     *
     * If the value isn't valid, then it won't be off.
     */
    static bool isRedundancyOffAtRuntime()
    {
        auto [valid, off] = getRedundancyOffAtRuntime();
        return valid && off;
    }

    /**
     * @brief Returns if the 'redundancy off at runtime' value is valid.
     */
    static bool isRedundancyOffAtRuntimeValid()
    {
        auto [valid, _] = getRedundancyOffAtRuntime();
        return valid;
    }

    /**
     * @brief Clears both the valid and value fields
     *        of 'redundancy off at runtime'.
     */
    static void clearRedundancyOffAtRuntime()
    {
        setRedundancyOffAtRuntime(false, false);
    }

    /**
     * @brief Sets a valid value for 'redundancy off at runtime'.
     *
     * @param[in] off - If off or not
     */
    static void setRedundancyOffAtRuntimeValue(bool off)
    {
        setRedundancyOffAtRuntime(true, off);
    }

    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;

    /**
     * @brief Provides the various system interfaces
     */
    Providers& providers;

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

    /**
     * @brief The current system state value
     */
    std::optional<SystemState> systemState;

    /**
     * @brief If data sync is in a failed state.
     */
    bool syncFailed = false;
};

} // namespace rbmc
