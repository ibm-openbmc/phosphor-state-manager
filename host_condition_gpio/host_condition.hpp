#pragma once

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Condition/HostFirmware/server.hpp>

#include <iostream>

namespace phosphor
{
namespace condition
{

using HostIntf = sdbusplus::server::object_t<
    sdbusplus::server::xyz::openbmc_project::condition::HostFirmware>;

class Host : public HostIntf
{
  public:
    Host() = delete;
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    Host(Host&&) = delete;
    Host& operator=(Host&&) = delete;
    ~Host() override = default;

    Host(sdbusplus::bus_t& bus, const std::string& path,
         const std::string& hostId) :
        HostIntf(bus, path.c_str()),
        lineName("host" + hostId)
    {
        scanGpioPin();
    };

    /** @brief Override reads to CurrentFirmwareCondition */
    FirmwareCondition currentFirmwareCondition() const override;

  private:
    std::string lineName;
    bool isActHigh;

    /*
     * Scan gpio pin to detect the name and active state
     */
    void scanGpioPin();
};
} // namespace condition
} // namespace phosphor
