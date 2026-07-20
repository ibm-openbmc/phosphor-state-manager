// SPDX-License-Identifier: Apache-2.0
#include "code_update_activation.hpp"
#include "persistent_data.hpp"
#include "persistent_data_test_fixture.hpp"

#include <sdbusplus/async.hpp>

#include <gtest/gtest.h>

using namespace rbmc;

class CodeUpdateActivationTest : public rbmc::test::PersistentDataTestFixture
{
  protected:
    ~CodeUpdateActivationTest() noexcept override = default;

    sdbusplus::async::context ctx;
};

/**
 * @brief Test: Default construction starts with no code update in progress.
 */
TEST_F(CodeUpdateActivationTest, DefaultsToNotInProgress)
{
    CodeUpdateActivation activation{ctx};

    EXPECT_FALSE(activation.codeUpdateInProgress());
    // The file won't exist when value is false on construction.
    EXPECT_FALSE(data::read<bool>(data::key::codeUpdateInProgress).has_value());
}

/**
 * @brief Test: Constructor restores a persisted in-progress state.
 */
TEST_F(CodeUpdateActivationTest, StartupRestoresInProgressFromFile)
{
    data::write(data::key::codeUpdateInProgress, true);

    CodeUpdateActivation activation{ctx};

    EXPECT_TRUE(activation.codeUpdateInProgress());
}

/**
 * @brief Test: Constructor restores a persisted not-in-progress state.
 */
TEST_F(CodeUpdateActivationTest, StartupRestoresNotInProgressFromFile)
{
    data::write(data::key::codeUpdateInProgress, false);

    CodeUpdateActivation activation{ctx};

    EXPECT_FALSE(activation.codeUpdateInProgress());
}

/**
 * @brief Test: setCodeUpdateInProgress sets the in-memory state and
 *             persists it to the filesystem.
 */
TEST_F(CodeUpdateActivationTest, SetAndClearInProgress)
{
    CodeUpdateActivation activation{ctx};

    activation.setCodeUpdateInProgress();

    EXPECT_TRUE(activation.codeUpdateInProgress());
    EXPECT_TRUE(
        data::read<bool>(data::key::codeUpdateInProgress).value_or(false));

    activation.clearCodeUpdateInProgress();

    EXPECT_FALSE(activation.codeUpdateInProgress());
    EXPECT_FALSE(
        data::read<bool>(data::key::codeUpdateInProgress).value_or(true));
}
