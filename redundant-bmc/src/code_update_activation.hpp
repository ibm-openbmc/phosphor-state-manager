// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/Software/Activation/aserver.hpp>

namespace rbmc
{

/**
 * @class CodeUpdateActivation
 *
 * Hosts the code-update-in-progress Activation interface on the
 * RBMC state object path.
 */
class CodeUpdateActivation :
    public sdbusplus::aserver::xyz::openbmc_project::software::Activation<
        CodeUpdateActivation>
{
  public:
    CodeUpdateActivation(const CodeUpdateActivation&) = delete;
    CodeUpdateActivation& operator=(const CodeUpdateActivation&) = delete;
    CodeUpdateActivation(CodeUpdateActivation&&) = delete;
    CodeUpdateActivation& operator=(CodeUpdateActivation&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     */
    explicit CodeUpdateActivation(sdbusplus::async::context& ctx);

    /**
     * @brief Returns if code update is in progress
     */
    bool codeUpdateInProgress() const;

    /**
     * @brief Marks a code update as in progress
     */
    void setCodeUpdateInProgress();

    /**
     * @brief Clears the code update in progress value
     */
    void clearCodeUpdateInProgress();

    /**
     * @brief Handles updates to the Activation property
     *
     * Updates the stored D-Bus value and persists the corresponding
     * code-update-in-progress state.
     *
     * @param[in] type - The property tag type
     * @param[in] activation - The requested value
     *
     * @return true if the property value changed
     */
    bool set_property(activation_t type, Activations activation);

  private:
    /**
     * @brief The D-Bus object path for the interface
     */
    static const std::string objectPath;
};

} // namespace rbmc
