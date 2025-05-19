// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "services.hpp"
#include "sibling.hpp"
#include "sync_interface.hpp"

namespace rbmc
{

/**
 * @class Providers
 *
 * Holders the helper classes that provide access
 * to various pieces of the system.
 */
class Providers
{
  public:
    Providers() = default;
    virtual ~Providers() = default;
    Providers(const Providers&) = delete;
    Providers& operator=(const Providers&) = delete;
    Providers(Providers&&) = delete;
    Providers& operator=(Providers&&) = delete;

    /**
     * @brief Returns the Services provider
     */
    virtual Services& getServices() = 0;

    /**
     * @brief Returns the Sibling provider
     */
    virtual Sibling& getSibling() = 0;

    /**
     * @brief Returns the SyncInterface provider
     */
    virtual SyncInterface& getSyncInterface() = 0;
};

}; // namespace rbmc
