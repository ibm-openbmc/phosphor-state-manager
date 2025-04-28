// SPDX-License-Identifier: Apache-2.0

#include <phosphor-logging/lg2.hpp>
#include <sync_interface_impl.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>

#include <ranges>

namespace rbmc
{

using Mapper = sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;

// NOLINTNEXTLINE
sdbusplus::async::task<bool> SyncInterfaceImpl::doFullSync()
{
    using SyncStatus = SyncBMCData::FullSyncStatus;
    SyncStatus status = SyncStatus::Unknown;

    try
    {
        co_await lookupService();

        auto sync = SyncBMCData(ctx)
                        .service(syncService)
                        .path(SyncBMCData::instance_path);

        // Ensure background sync is enabled.
        co_await sync.disable_sync(false);

        status = co_await sync.full_sync_status();

        fullSyncInProgress = true;
        if (status != SyncStatus::FullSyncInProgress)
        {
            lg2::info("Starting full sync and waiting for completion");
            co_await sync.start_full_sync();
        }
        else
        {
            // A full sync is already running, don't need to start one.
            lg2::info(
                "A full sync is already in progress, waiting for completion");
        }

        namespace rules = sdbusplus::bus::match::rules;
        sdbusplus::async::match match(
            ctx, rules::propertiesChanged(SyncBMCData::instance_path,
                                          SyncBMCData::interface));

        status = co_await sync.full_sync_status();

        while ((status == SyncStatus::FullSyncInProgress) &&
               !ctx.stop_requested())
        {
            using PropertyMap =
                std::unordered_map<std::string, SyncBMCData::PropertiesVariant>;
            auto [_,
                  properties] = co_await match.next<std::string, PropertyMap>();

            auto it = properties.find("FullSyncStatus");
            if (it != properties.end())
            {
                status = std::get<SyncStatus>(it->second);
            }
        }
    }
    catch (const std::exception& e)
    {
        fullSyncInProgress = false;
        throw;
    }

    lg2::info("Full sync completed with status {STATUS}", "STATUS", status);

    fullSyncInProgress = false;
    co_return status == SyncStatus::FullSyncCompleted;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> SyncInterfaceImpl::lookupService()
{
    if (!syncService.empty())
    {
        co_return;
    }

    auto object =
        co_await Mapper(ctx)
            .service(Mapper::default_service)
            .path(Mapper::instance_path)
            .get_object(SyncBMCData::instance_path, {SyncBMCData::interface});

    syncService = object.begin()->first;
    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> SyncInterfaceImpl::disableBackgroundSync()
{
    try
    {
        co_await lookupService();

        co_await SyncBMCData(ctx)
            .service(syncService)
            .path(SyncBMCData::instance_path)
            .disable_sync(true);
    }
    catch (const std::exception& e)
    {
        lg2::error("Call to disable sync failed: {ERROR}:", "ERROR", e);
    }

    co_return;
}

sdbusplus::async::task<>
    // NOLINTNEXTLINE
    SyncInterfaceImpl::watchSyncEventsHealthPropertyChanged()
{
    namespace rules = sdbusplus::bus::match::rules;
    sdbusplus::async::match match(
        ctx, rules::propertiesChanged(SyncBMCData::instance_path,
                                      SyncBMCData::interface));

    while (!ctx.stop_requested())
    {
        using PropertyMap =
            std::unordered_map<std::string, SyncBMCData::PropertiesVariant>;

        auto [_, properties] = co_await match.next<std::string, PropertyMap>();

        if (auto it = properties.find("SyncEventsHealth");
            it != properties.end())
        {
            for (const auto& callback :
                 std::ranges::views::values(healthCallbacks))
            {
                callback(std::get<SyncBMCData::SyncEventsHealth>(it->second));
            }
        }
    }

    co_return;
}

}; // namespace rbmc
