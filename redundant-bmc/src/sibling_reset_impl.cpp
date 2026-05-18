// SPDX-License-Identifier: Apache-2.0

#include "sibling_reset_impl.hpp"

#include "gpio.hpp"

namespace rbmc
{

SiblingResetImpl::SiblingResetImpl(sdbusplus::async::context& ctx,
                                   const RedundantBMCConfig& config) :
    ctx(ctx), polarity(config.siblingBMCResetGPIO.polarity)
{
    resetLine = gpiod::find_line(config.siblingBMCResetGPIO.name);
    if (!resetLine)
    {
        throw std::runtime_error(
            "Could not find BMC reset GPIO " + config.siblingBMCResetGPIO.name);
    }
}

void SiblingResetImpl::assertReset()
{
    // TODO - Concurrent Maintenance design pending
    throw std::runtime_error{"assertReset not implemented yet"};
}

void SiblingResetImpl::releaseReset()
{
    // TODO - Concurrent Maintenance design pending
    throw std::runtime_error{"releaseReset not implemented yet"};
}

// NOLINTBEGIN(clang-analyzer-core.uninitialized.Branch)
sdbusplus::async::task<> SiblingResetImpl::toggleReset()
{
    if (!resetLine)
    {
        throw std::runtime_error("Could not find sibling reset GPIO");
    }

    lg2::info("Toggling sibling reset GPIO");

    using namespace std::chrono_literals;
    co_await gpio::toggleGPIO(resetLine, polarity, ctx, 200ms);
}
// NOLINTEND(clang-analyzer-core.uninitialized.Branch)

} // namespace rbmc
