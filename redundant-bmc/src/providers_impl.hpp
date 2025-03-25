// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "providers.hpp"
#include "services_impl.hpp"
#include "sibling_impl.hpp"

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
        services(ctx), sibling(ctx)
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

  private:
    /**
     * @brief The Services implementation
     */
    ServicesImpl services;

    /**
     * @brief The Sibling implementation
     */
    SiblingImpl sibling;
};

}; // namespace rbmc
