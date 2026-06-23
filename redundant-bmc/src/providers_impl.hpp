// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "config_parser.hpp"
#include "providers.hpp"
#include "services_impl.hpp"
#include "sibling_impl.hpp"
#include "sibling_reset_impl.hpp"
#include "sync_interface_impl.hpp"

#include <phosphor-logging/lg2.hpp>

#include <optional>

namespace rbmc
{

/**
 * @class ProvidersImpl
 *
 * Holds the real providers classes.
 *
 */
class ProvidersImpl : public Providers
{
  public:
    ~ProvidersImpl() override = default;
    ProvidersImpl(const ProvidersImpl&) = delete;
    ProvidersImpl& operator=(const ProvidersImpl&) = delete;
    ProvidersImpl(ProvidersImpl&&) = delete;
    ProvidersImpl& operator=(ProvidersImpl&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     */
    explicit ProvidersImpl(sdbusplus::async::context& ctx) :
        config(config_parser::readConfig()), services(ctx),
        sibling(ctx, config, services), syncInterface(ctx),
        siblingReset(ctx, config), pcieStorage(createPCIeStorage())
    {}

    /**
     * @brief Returns the Services provider
     */
    Services& getServices() override
    {
        return services;
    }

    /**
     * @brief Returns the Sibling provider
     */
    Sibling& getSibling() override
    {
        return sibling;
    }

    /**
     * @brief Returns the SyncInterface provider
     */
    SyncInterface& getSyncInterface() override
    {
        return syncInterface;
    }

    /**
     * @brief Returns the SiblingReset provider
     */
    SiblingReset& getSiblingReset() override
    {
        return siblingReset;
    }

    /**
     * @brief Returns the PCIeStorage provider
     */
    pcie_data::PCIeStorage* getPCIeStorage() override
    {
        if (!pcieStorage)
        {
            return nullptr;
        }
        return &*pcieStorage;
    }

  private:
    /**
     * @brief Create PCIeStorage if config is present
     *
     * @return Optional PCIeStorageImpl
     */
    std::optional<pcie_data::PCIeStorageImpl> createPCIeStorage()
    {
        if (!config.pcieConfig.has_value())
        {
            lg2::debug("PCIe storage not configured - pcie_config not present");
            return std::nullopt;
        }

        const auto& pcieConf = config.pcieConfig.value();
        // Parse offset string (supports decimal and hex with 0x prefix)
        size_t offset = std::stoull(pcieConf.redundancyOffset, nullptr, 0);
        return std::make_optional<pcie_data::PCIeStorageImpl>(
            pcieConf.devicePath, offset);
    }

    /**
     * @brief The parsed configuration
     */
    RedundantBMCConfig config;

    /**
     * @brief The Services implementation
     */
    ServicesImpl services;

    /**
     * @brief The Sibling implementation
     */
    SiblingImpl sibling;

    /**
     * @brief The SyncInterface implementation
     */
    SyncInterfaceImpl syncInterface;

    /**
     * @brief The SiblingReset implementation
     */
    SiblingResetImpl siblingReset;

    /**
     * @brief The PCIeStorage implementation (optional)
     */
    std::optional<pcie_data::PCIeStorageImpl> pcieStorage;
};

}; // namespace rbmc
