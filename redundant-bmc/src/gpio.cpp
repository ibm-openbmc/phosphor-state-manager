// SPDX-License-Identifier: Apache-2.0
#include "gpio.hpp"

#include <gpiod.hpp>
#include <phosphor-logging/lg2.hpp>

#include <chrono>
#include <thread>

namespace rbmc
{

namespace
{

class GPIOLineGuard
{
  public:
    explicit GPIOLineGuard(gpiod::line& line) : line(line) {}

    ~GPIOLineGuard()
    {
        if (line.is_requested())
        {
            try
            {
                line.release();
            }
            catch (...)
            {}
        }
    }

    GPIOLineGuard(const GPIOLineGuard&) = delete;
    GPIOLineGuard& operator=(const GPIOLineGuard&) = delete;
    GPIOLineGuard(GPIOLineGuard&&) = delete;
    GPIOLineGuard& operator=(GPIOLineGuard&&) = delete;

  private:
    gpiod::line& line;
};

} // anonymous namespace

std::optional<bool> readGPIO(const std::string& gpioName, GPIOPolarity polarity)
{
    constexpr int maxRetries = 3;
    constexpr std::chrono::milliseconds retryDelay{5};

    auto line = gpiod::find_line(gpioName);
    if (!line)
    {
        lg2::warning("GPIO line {NAME} not found", "NAME", gpioName);
        return std::nullopt;
    }

    const bool activeHigh = (polarity == GPIOPolarity::high);

    // Retry in case of contention with other GPIO readers
    for (int attempt = 1; attempt <= maxRetries; ++attempt)
    {
        try
        {
            line.request(
                {"RBMC manager", gpiod::line_request::DIRECTION_INPUT,
                 activeHigh ? 0 : gpiod::line_request::FLAG_ACTIVE_LOW});

            GPIOLineGuard guard(line);

            return line.get_value() != 0;
        }
        catch (const std::exception& e)
        {
            if (attempt < maxRetries)
            {
                lg2::info(
                    "GPIO {NAME} request failed (attempt {ATTEMPT}/{MAX}): {ERROR}",
                    "NAME", gpioName, "ATTEMPT", attempt, "MAX", maxRetries,
                    "ERROR", e);
                std::this_thread::sleep_for(retryDelay);
            }
            else
            {
                lg2::warning(
                    "Error reading GPIO {NAME} after {MAX} attempts: {ERROR}",
                    "NAME", gpioName, "MAX", maxRetries, "ERROR", e);
            }
        }
    }

    return std::nullopt;
}

} // namespace rbmc
