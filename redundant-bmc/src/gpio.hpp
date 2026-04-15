// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "types.hpp"

#include <optional>
#include <string>

namespace rbmc
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

} // namespace rbmc
