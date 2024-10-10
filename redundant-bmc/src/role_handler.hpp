// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "services.hpp"

#include <sdbusplus/async.hpp>

namespace rbmc
{

/**
 * @class RoleHandler
 *
 * This is the base class for the active and passive role
 * handler classes, which will handle all of the role specific
 * code.
 */
class RoleHandler
{
  public:
    RoleHandler() = delete;
    virtual ~RoleHandler() = default;
    RoleHandler(const RoleHandler&) = delete;
    RoleHandler& operator=(const RoleHandler&) = delete;
    RoleHandler(RoleHandler&&) = delete;
    RoleHandler& operator=(RoleHandler&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     * @param[in] services - The services object
     */
    RoleHandler(sdbusplus::async::context& ctx,
                std::unique_ptr<Services>& services) :
        ctx(ctx), services(services)
    {}

    /**
     * @brief Pure virtual function to start the handler
     *        in an async context.
     */
    virtual sdbusplus::async::task<> start() = 0;

  protected:
    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;

    /**
     * @brief The services object
     */
    std::unique_ptr<Services>& services;
};

} // namespace rbmc
