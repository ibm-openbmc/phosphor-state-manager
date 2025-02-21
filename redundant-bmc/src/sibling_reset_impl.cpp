// SPDX-License-Identifier: Apache-2.0

#include "sibling_reset_impl.hpp"

#include <gpiod.hpp>
#include <phosphor-logging/lg2.hpp>

#include <cassert>

namespace rbmc
{

const std::string gpioName = "sibling-bmc-reset";

SiblingResetImpl::SiblingResetImpl()
{
    resetLine = gpiod::find_line(gpioName);
    if (!resetLine)
    {
        // Attempt to find the active low version.
        resetLine = gpiod::find_line(gpioName + "-n");
        if (resetLine)
        {
            activeLow = true;
        }
    }

    if (resetLine)
    {
        config.consumer = "Sibling BMC Reset";
        config.request_type = gpiod::line_request::DIRECTION_OUTPUT;
        config.flags = activeLow ? gpiod::line_request::FLAG_ACTIVE_LOW : 0;
    }
    else
    {
        // This will cause a fail during the assert/release
        lg2::error("Could not find BMC reset GPIO {GPIO}", "GPIO", gpioName);
    }
}

void SiblingResetImpl::assertReset()
{
    if (!resetLine)
    {
        throw std::runtime_error("Could not find sibling reset GPIO");
    }

    lg2::info("Asserting sibling BMC reset GPIO");

    resetLine.request(config, 1);
    resetLine.release();
}

void SiblingResetImpl::releaseReset()
{
    if (!resetLine)
    {
        throw std::runtime_error("Could not find sibling reset GPIO");
    }

    lg2::info("Releasing sibling BMC reset GPIO");

    resetLine.request(config, 0);
    resetLine.release();
}

} // namespace rbmc
