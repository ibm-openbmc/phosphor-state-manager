/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <sdbusplus/async.hpp>

namespace rbmc
{

/**
 * @class Services
 *
 * This is the base class interface for dealing with system
 * information, so that the details on how to obtain the data
 * are abtracted away from the business logic.
 *
 * In addition to a derived class the implements the functionality,
 * a mock version of the class can be used in unit test.
 *
 */
class Services
{
  public:
    Services() = default;
    virtual ~Services() = default;
    Services(const Services&) = delete;
    Services& operator=(const Services&) = delete;
    Services(Services&&) = delete;
    Services& operator=(Services&&) = delete;

    /**
     * @brief Returns this BMC's position.
     *
     * @return - The position
     */
    virtual size_t getBMCPosition() const = 0;

    /**
     * @brief Starts a systemd unit
     *
     * Waits for it to be active or failed before returning.
     *
     * @param[in] unitName - The unit name
     */
    virtual sdbusplus::async::task<>
        startUnit(const std::string& unitName) const = 0;
};

} // namespace rbmc
