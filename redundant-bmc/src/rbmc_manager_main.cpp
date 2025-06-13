/* SPDX-License-Identifier: Apache-2.0 */

#include "manager.hpp"
#include "providers_impl.hpp"

#include <sdbusplus/async/context.hpp>
#include <sdbusplus/server/manager.hpp>

int main()
{
    sdbusplus::async::context ctx;
    sdbusplus::server::manager_t objMgr{
        ctx, rbmc::RedundancyInterface::namespace_path::value};

    std::unique_ptr<rbmc::Providers> providers =
        std::make_unique<rbmc::ProvidersImpl>(ctx);

    rbmc::Manager manager{ctx, std::move(providers)};

    // NOLINTNEXTLINE
    ctx.spawn([](sdbusplus::async::context& ctx) -> sdbusplus::async::task<> {
        ctx.request_name(rbmc::RedundancyInterface::interface);
        co_return;
    }(ctx));

    ctx.run();

    return 0;
}
