// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "gpiod.hpp"
#include "types.hpp"

#include <sdbusplus/async.hpp>

#include <optional>
#include <string>

namespace rbmc::gpio
{

/**
 * @brief Read a GPIO
 *
 * @param[in] gpioName - The GPIO line name
 * @param[in] polarity - The GPIO polarity
 * @return true if GPIO reads high (after polarity adjustment),
 *         false if GPIO reads low,
 *         nullopt if GPIO line not found or read failed after retries
 */
std::optional<bool> readGPIO(const std::string& gpioName,
                             GPIOPolarity polarity);

/**
 * @brief Toggle a GPIO line
 *
 * @param[in] gpioName - The GPIO line name
 * @param[in] polarity - The GPIO polarity
 * @param[in] ctx - The async context for sleep
 * @param[in] delay - How long to hold the GPIO asserted
 * @return Async task that completes when toggle is done
 * @throws std::runtime_error if GPIO line not found or operation failed
 */
sdbusplus::async::task<> toggleGPIO(
    const std::string& gpioName, GPIOPolarity polarity,
    sdbusplus::async::context& ctx, std::chrono::milliseconds delay);

/**
 * @brief Toggle a GPIO line on an existing gpiod::line
 *
 * @param[in] line - The GPIO line
 * @param[in] polarity - The GPIO polarity
 * @param[in] ctx - The async context for sleep
 * @param[in] delay - How long to hold the GPIO asserted
 * @return Async task that completes when toggle is done
 * @throws std::runtime_error if operation failed after retries
 */
sdbusplus::async::task<> toggleGPIO(gpiod::line& line, GPIOPolarity polarity,
                                    sdbusplus::async::context& ctx,
                                    std::chrono::milliseconds delay);

} // namespace rbmc::gpio
