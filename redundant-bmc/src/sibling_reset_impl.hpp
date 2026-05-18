// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "config_data.hpp"
#include "sibling_reset.hpp"
#include "types.hpp"

#include <gpiod.hpp>
#include <phosphor-logging/lg2.hpp>

namespace rbmc
{

/**
 * @class SiblingResetImpl
 *
 * Implementation to handle resetting the sibling BMC.
 */
class SiblingResetImpl : public SiblingReset
{
  public:
    ~SiblingResetImpl() override = default;
    SiblingResetImpl(const SiblingResetImpl&) = delete;
    SiblingResetImpl& operator=(const SiblingResetImpl&) = delete;
    SiblingResetImpl(SiblingResetImpl&&) = delete;
    SiblingResetImpl& operator=(SiblingResetImpl&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     * @param[in] config - Optional redundant BMC configuration
     */
    SiblingResetImpl(sdbusplus::async::context& ctx,
                     const RedundantBMCConfig& config);

    /**
     * @brief Asserts the reset.
     *
     * Releases the GPIO request on completion.
     */
    void assertReset() override;

    /**
     * @brief Releases the reset.
     *
     * Releases the GPIO request on completion.
     */
    void releaseReset() override;

    /**
     * @brief Toggles the GPIO to do the full reset
     */
    sdbusplus::async::task<> toggleReset() override;

  private:
    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;

    /**
     * @brief The GPIO polarity
     */
    GPIOPolarity polarity;

    /**
     * @brief The reset line
     */
    gpiod::line resetLine;
};

} // namespace rbmc
