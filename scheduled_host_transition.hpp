#pragma once

#include <sdbusplus/bus.hpp>
#include <phosphor-logging/log.hpp>
#include <xyz/openbmc_project/State/ScheduledHostTransition/server.hpp>

namespace phosphor
{
namespace state
{
namespace manager
{

using ScheduledHostTransitionInherit = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::State::server::ScheduledHostTransition>;

/** @class ScheduledHostTransition
 *  @brief Scheduled host transition implementation.
 *  @details A concrete implementation for
 *  xyz.openbmc_project.State.ScheduledHostTransition
 */
class ScheduledHostTransition : public ScheduledHostTransitionInherit
{
  public:
    ScheduledHostTransition(sdbusplus::bus::bus& bus, const char* objPath) :
        ScheduledHostTransitionInherit(bus, objPath)
    {
    }

    ~ScheduledHostTransition() = default;

    /**
     * @brief Handle with scheduled time
     *
     * @param[in] value - The seconds since epoch
     * @return The time for the transition. It is the same as the input value if
     * it is set successfully. Otherwise, it won't return value, but throw an
     * error.
     **/
    uint64_t scheduledTime(uint64_t value) override;
};
} // namespace manager
} // namespace state
} // namespace phosphor