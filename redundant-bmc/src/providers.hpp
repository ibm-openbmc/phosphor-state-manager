// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "services.hpp"
#include "sibling.hpp"

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
};

}; // namespace rbmc
