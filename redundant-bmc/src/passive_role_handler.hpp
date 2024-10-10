// SPDX-License-Identifier: Apache-2.0
#include "role_handler.hpp"

namespace rbmc
{

/**
 * @class PassiveRoleHandler
 *
 * This class handles operation specific to the passive role.
 */
class PassiveRoleHandler : public RoleHandler
{
  public:
    PassiveRoleHandler() = delete;
    ~PassiveRoleHandler() override = default;
    PassiveRoleHandler(const PassiveRoleHandler&) = delete;
    PassiveRoleHandler& operator=(const PassiveRoleHandler&) = delete;
    PassiveRoleHandler(PassiveRoleHandler&&) = delete;
    PassiveRoleHandler& operator=(PassiveRoleHandler&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     * @param[in] services - The services object
     */
    PassiveRoleHandler(sdbusplus::async::context& ctx,
                       std::unique_ptr<Services>& services) :
        RoleHandler(ctx, services)
    {}

    /**
     * @brief Starts the handler.
     */
    sdbusplus::async::task<> start() override;
};

} // namespace rbmc
