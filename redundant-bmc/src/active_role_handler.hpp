// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "role_handler.hpp"

namespace rbmc
{

/**
 * @class ActiveRoleHandler
 *
 * This class handles operation specific to the active role.
 */
class ActiveRoleHandler : public RoleHandler
{
  public:
    ActiveRoleHandler() = delete;
    ~ActiveRoleHandler() override = default;
    ActiveRoleHandler(const ActiveRoleHandler&) = delete;
    ActiveRoleHandler& operator=(const ActiveRoleHandler&) = delete;
    ActiveRoleHandler(ActiveRoleHandler&&) = delete;
    ActiveRoleHandler& operator=(ActiveRoleHandler&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     * @param[in] services - The services object
     */
    ActiveRoleHandler(sdbusplus::async::context& ctx,
                      std::unique_ptr<Services>& services) :
        RoleHandler(ctx, services)
    {}

    /**
     * @brief Starts the handler.
     */
    sdbusplus::async::task<> start() override;
};

} // namespace rbmc
