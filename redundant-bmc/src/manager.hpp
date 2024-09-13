/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/server.hpp>

namespace rbmc
{

using RedundancyIntf =
    sdbusplus::xyz::openbmc_project::State::BMC::server::Redundancy;
using RedundancyInterface = sdbusplus::server::object_t<RedundancyIntf>;

/**
 * @class Manager
 *
 * Manages the high level operations of the redundant
 * BMC functionality.
 */
class Manager
{
  public:
    Manager() = delete;
    ~Manager() = default;
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    Manager(Manager&&) = delete;
    Manager& operator=(Manager&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     */
    explicit Manager(sdbusplus::async::context& ctx);

  private:
    /**
     * @brief Kicks off the Manager startup
     */
    sdbusplus::async::task<> startup();

    /**
     * @brief Emits a heartbeat signal every second
     *
     * The sibling gets this via the sibling app to
     * know all's well.
     */
    sdbusplus::async::task<> doHeartBeat();

    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;

    /**
     * @brief The Redundancy D-Bus interface
     */
    RedundancyInterface redundancyInterface;
};

} // namespace rbmc
