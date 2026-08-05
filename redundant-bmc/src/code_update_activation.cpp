// SPDX-License-Identifier: Apache-2.0
#include "code_update_activation.hpp"

#include "persistent_data.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>

namespace rbmc
{

using Redundancy =
    sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy;

const std::string CodeUpdateActivation::objectPath =
    std::string{Redundancy::namespace_path::value} + '/' +
    Redundancy::namespace_path::bmc;

CodeUpdateActivation::CodeUpdateActivation(sdbusplus::async::context& ctx) :
    sdbusplus::aserver::xyz::openbmc_project::software::Activation<
        CodeUpdateActivation>(ctx, objectPath.c_str())
{
    try
    {
        properties.activation =
            data::read<bool>(data::key::codeUpdateInProgress).value_or(false)
                ? Activations::Activating
                : Activations::Active;
    }
    catch (const std::exception& e)
    {
        properties.activation = Activations::Active;
        lg2::error("Failed reading code-update-in-progress state: {ERROR}",
                   "ERROR", e);
    }

    if (properties.activation == Activations::Activating)
    {
        lg2::info("Code update in progress on startup");
    }

    properties.requested_activation = RequestedActivations::None;
    emit_added();
}

bool CodeUpdateActivation::codeUpdateInProgress() const
{
    return properties.activation == Activations::Activating;
}

void CodeUpdateActivation::setCodeUpdateInProgress()
{
    activation(Activations::Activating);
}

void CodeUpdateActivation::clearCodeUpdateInProgress()
{
    activation(Activations::Active);
}

bool CodeUpdateActivation::set_property([[maybe_unused]] activation_t type,
                                        Activations activation)
{
    if (activation == properties.activation)
    {
        return false;
    }

    lg2::info("Activation property changing to {VALUE}", "VALUE", activation);

    properties.activation = activation;

    // This needs to be saved through a reboot, so persist it.
    try
    {
        data::write(data::key::codeUpdateInProgress,
                    properties.activation == Activations::Activating);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed serializing code-update-in-progress state: {ERROR}",
                   "ERROR", e);
    }

    return true;
}

} // namespace rbmc
