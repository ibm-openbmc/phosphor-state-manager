/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "services.hpp"

namespace rbmc
{

/**
 * @class ServicesImpl
 *
 * Implements the Services functions to interact
 * with the system.
 *
 */
class ServicesImpl : public Services
{
  public:
    ServicesImpl() = default;
    ~ServicesImpl() override = default;
    ServicesImpl(const ServicesImpl&) = delete;
    ServicesImpl& operator=(const ServicesImpl&) = delete;
    ServicesImpl(ServicesImpl&&) = delete;
    ServicesImpl& operator=(ServicesImpl&&) = delete;

    /**
     * @brief Returns this BMC's position.
     *
     * @return - The position
     */
    size_t getBMCPosition() const override;
};

} // namespace rbmc
