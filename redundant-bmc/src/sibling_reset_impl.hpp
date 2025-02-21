// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "sibling_reset.hpp"

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
     */
    SiblingResetImpl();

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

  private:
    /**
     * @brief The GPIO config for requesting a line.
     */
    gpiod::line_request config;

    /**
     * @brief The reset line
     */
    gpiod::line resetLine;

    /**
     * @brief If the GPIO is active low
     */
    bool activeLow{false};
};

} // namespace rbmc
