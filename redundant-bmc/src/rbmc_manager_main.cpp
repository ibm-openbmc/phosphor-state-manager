/* SPDX-License-Identifier: Apache-2.0 */

#include "manager.hpp"
#include "services_impl.hpp"
#include "sibling_impl.hpp"

#include <sdbusplus/async/context.hpp>
#include <sdbusplus/server/manager.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>

using Redundancy =
    sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy;

int main()
{
    sdbusplus::async::context ctx;
    sdbusplus::server::manager_t objMgr{ctx, "/xyz/openbmc_project/state"};

    std::unique_ptr<rbmc::Services> services =
        std::make_unique<rbmc::ServicesImpl>(ctx);
    std::unique_ptr<rbmc::Sibling> sibling =
        std::make_unique<rbmc::SiblingImpl>(ctx);

    rbmc::Manager manager{ctx, std::move(services), std::move(sibling)};

    // clang-tidy currently mangles this into something unreadable
    // NOLINTNEXTLINE
    ctx.spawn([](sdbusplus::async::context& ctx) -> sdbusplus::async::task<> {
        ctx.request_name(Redundancy::interface);
        co_return;
    }(ctx));

    ctx.run();

    return 0;
}
