// SPDX-License-Identifier: Apache-2.0
#include "manager.hpp"
#include "mocks/async_helpers.hpp"
#include "mocks/mock_providers.hpp"
#include "persistent_data_test_fixture.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace rbmc;
using namespace testing;
using namespace test_helpers;

using Redundancy =
    sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy;

constexpr std::chrono::milliseconds hbInterval{5};

class ManagerTest : public rbmc::test::PersistentDataTestFixture
{
  protected:
    struct TestScenarioConfig
    {
        bool paired = true;
        std::optional<size_t> bmcPosition = 0;
        bool systemInventoryAvailable = true;
        bool siblingServiceRunning = true;
        bool siblingPresent = true;
        bool siblingAlive = true;
        Role siblingRole = Role::Passive;
        bool siblingPaired = true;
        bool siblingFailoverInProgress = false;
    };

    ~ManagerTest() noexcept override = default;

    void SetUp() override
    {
        mockProviders = std::make_unique<MockProviders>();

        ON_CALL(mockProviders->getMockServices(), getPersistentDataPath())
            .WillByDefault(Return(dataDir));
    }

    void TearDown() override
    {
        manager.reset();
        mockProviders.reset();
    }

    /**
     * @brief Set what the functions should return for a
     *        specific test scenario.
     */
    void setupTestScenario(const TestScenarioConfig& config)
    {
        auto& services = mockProviders->getMockServices();
        auto& sibling = mockProviders->getMockSibling();

        ON_CALL(services, getPaired()).WillByDefault(Return(config.paired));

        ON_CALL(services, getBMCPosition())
            .WillByDefault(Return(config.bmcPosition));

        ON_CALL(services, checkSystemInventoryStatus())
            .WillByDefault([available = config.systemInventoryAvailable]() {
                return makeCompletedTask(available);
            });

        if (config.siblingServiceRunning)
        {
            siblingServiceName =
                "xyz.openbmc_project.State.BMC.Redundancy.Sibling";
        }
        else
        {
            siblingServiceName = "";
        }

        ON_CALL(sibling, getServiceName())
            .WillByDefault(ReturnRef(siblingServiceName));

        ON_CALL(sibling, isBMCPresent())
            .WillByDefault(Return(config.siblingPresent));

        ON_CALL(sibling, alive()).WillByDefault(Return(config.siblingAlive));

        ON_CALL(sibling, getRole()).WillByDefault(Return(config.siblingRole));

        ON_CALL(sibling, getPaired())
            .WillByDefault(Return(config.siblingPaired));

        ON_CALL(sibling, getFailoverInProgress())
            .WillByDefault(Return(config.siblingFailoverInProgress));
    }

    struct RedundancyProps
    {
        Role role{};
        bool redEnabled{};
        bool failoverInProgress{};
        bool failoversAllowed{};
        bool failoverImminent{};
        std::vector<Redundancy::ReasonForNoRedundancy> reasonsForNoRedundancy;
        FailoversNotAllowedReason failoversNotAllowedReason;
    };

    static void verifyRedundancyProps(const RedundancyInterface& iface,
                                      const RedundancyProps& expected)
    {
        EXPECT_EQ(iface.role(), expected.role);
        EXPECT_EQ(iface.redundancy_enabled(), expected.redEnabled);
        EXPECT_EQ(iface.failover_in_progress(), expected.failoverInProgress);
        EXPECT_EQ(iface.failovers_allowed(), expected.failoversAllowed);
        EXPECT_EQ(iface.failover_imminent(), expected.failoverImminent);
        EXPECT_EQ(iface.reasons_for_no_redundancy(),
                  expected.reasonsForNoRedundancy);
        EXPECT_EQ(iface.failovers_not_allowed_reason(),
                  expected.failoversNotAllowedReason);
    }

    void setupPCIeStorageExpects(const RedundancyProps& vals)
    {
        auto& storage = mockProviders->getMockPCIeStorage();

        // Constructor writes full initial state (role=Unknown) via writeState
        EXPECT_CALL(storage, writeState(_)).Times(1);

        // Role always changes from Unknown to the final role
        EXPECT_CALL(storage, updateRole(static_cast<uint8_t>(vals.role)))
            .Times(1);

        // Boolean properties only fire if the value differs from the default
        // (false), so allow zero or one call each
        EXPECT_CALL(storage, updateRedundancyEnabled(vals.redEnabled))
            .Times(vals.redEnabled ? 1 : 0);
        EXPECT_CALL(storage, updateFailoverInProgress(vals.failoverInProgress))
            .Times(vals.failoverInProgress ? 1 : 0);
        EXPECT_CALL(storage, updateFailoversAllowed(vals.failoversAllowed))
            .Times(vals.failoversAllowed ? 1 : 0);
    }

    /**
     * @brief Create the Manager and let it run until a progress point is hit.
     */
    void createManagerAndRun(ProgressPoint point)
    {
        // Spawns Manager::startup()
        manager = std::make_unique<Manager>(ctx, std::move(mockProviders),
                                            hbInterval);

        ctx.spawn(waitForProgressPoint(point));

        ctx.run();
    }

    /**
     * @brief Let the context run until a progress point has been reached
     *
     * @param[in] point - The point to wait for
     * @param[in] timeout - The timeout, default is 1s
     * @param[in] stopWhenDone - If the context should be stopped afterwards.
     */
    sdbusplus::async::task<> waitForProgressPoint(ProgressPoint point,
                                                  bool stopWhenDone = true)
    {
        using namespace std::chrono_literals;
        auto deadline = std::chrono::steady_clock::now() + 1s;

        while (std::chrono::steady_clock::now() < deadline)
        {
            if (manager->getProviders().getTracker().hasReached(point))
            {
                if (stopWhenDone)
                {
                    ctx.request_stop();
                }
                co_return;
            }
            co_await sdbusplus::async::sleep_for(ctx, 10ms);
        }

        ADD_FAILURE() << "Timed out waiting for progress point "
                      << std::to_underlying(point);

        // If timed out, stop regardless
        ctx.request_stop();
    }

    /**
     * @brief Verify 3 values in the persistent data.
     */
    static void verifyPersistentData(Role expectedRole,
                                     const std::string& expectedRoleReason,
                                     bool expectPassiveError = false)
    {
        // Verify role was saved
        auto savedRole = data::read<Role>(data::key::role);
        ASSERT_TRUE(savedRole.has_value())
            << "Role should be saved to persistent data";
        EXPECT_EQ(savedRole.value(), expectedRole);

        // Verify passiveError flag was saved
        auto savedPassiveError = data::read<bool>(data::key::passiveError);
        ASSERT_TRUE(savedPassiveError.has_value())
            << "PassiveError flag should be saved to persistent data";
        EXPECT_EQ(savedPassiveError.value(), expectPassiveError);

        // Verify role reason description was saved
        auto savedRoleReason = data::read<std::string>(data::key::roleReason);
        ASSERT_TRUE(savedRoleReason.has_value())
            << "RoleReason should be saved to persistent data";
        EXPECT_EQ(savedRoleReason.value(), expectedRoleReason);
    }

    std::unique_ptr<MockProviders> mockProviders;
    std::unique_ptr<Manager> manager;
    std::string siblingServiceName;
    std::chrono::seconds passiveTargetTimeout{300};
    sdbusplus::async::context ctx;
};

/**
 * @brief Test: BMC becomes passive when not paired
 */
TEST_F(ManagerTest, BecomesPassive_NotPaired)
{
    // Configure scenario: BMC is not paired
    TestScenarioConfig config{.paired = false};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();

    EXPECT_CALL(services, logError(errors::error_msg::bmcIsPassiveDueToError,
                                   errors::Level::Error, _))
        .Times(1);

    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", passiveTargetTimeout))
        .Times(1);

    RedundancyProps expectedProps{
        .role = Role::Passive,
        .redEnabled = false,
        .failoverInProgress = false,
        .failoversAllowed = false,
        .failoverImminent = false,
        .reasonsForNoRedundancy =
            {Redundancy::ReasonForNoRedundancy::SiblingCannotBeActive},
        .failoversNotAllowedReason = FailoversNotAllowedReason::None};

    setupPCIeStorageExpects(expectedProps);

    createManagerAndRun(ProgressPoint::passiveHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(expectedProps.role, "BMC is not paired", true);
}

/**
 * @brief Test: BMC becomes passive when BMC position is unknown
 */
TEST_F(ManagerTest, BecomesPassive_NoBMCPosition)
{
    // Configure scenario: BMC position is unknown
    TestScenarioConfig config{.bmcPosition = std::nullopt};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();

    EXPECT_CALL(services, logError(errors::error_msg::bmcIsPassiveDueToError,
                                   errors::Level::Error, _))
        .Times(1);

    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", passiveTargetTimeout))
        .Times(1);

    RedundancyProps expectedProps{
        .role = Role::Passive,
        .redEnabled = false,
        .failoverInProgress = false,
        .failoversAllowed = false,
        .failoverImminent = false,
        .reasonsForNoRedundancy =
            {Redundancy::ReasonForNoRedundancy::SiblingCannotBeActive},
        .failoversNotAllowedReason = FailoversNotAllowedReason::None};

    setupPCIeStorageExpects(expectedProps);

    createManagerAndRun(ProgressPoint::passiveHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(expectedProps.role, "Cannot determine BMC position",
                         true);
}

/**
 * @brief Test: BMC becomes passive when system inventory is not available
 */
TEST_F(ManagerTest, BecomesPassive_SystemInventoryNotAvailable)
{
    // Configure scenario: System inventory is not available
    TestScenarioConfig config{.systemInventoryAvailable = false};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();

    // Verify error log is created for passive due to error
    EXPECT_CALL(services, logError(errors::error_msg::bmcIsPassiveDueToError,
                                   errors::Level::Error, _))
        .Times(1);

    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", passiveTargetTimeout))
        .Times(1);

    RedundancyProps expectedProps{
        .role = Role::Passive,
        .redEnabled = false,
        .failoverInProgress = false,
        .failoversAllowed = false,
        .failoverImminent = false,
        .reasonsForNoRedundancy =
            {Redundancy::ReasonForNoRedundancy::SiblingCannotBeActive},
        .failoversNotAllowedReason = FailoversNotAllowedReason::None};

    setupPCIeStorageExpects(expectedProps);

    createManagerAndRun(ProgressPoint::passiveHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(expectedProps.role,
                         "System inventory is not available", true);
}

/**
 * @brief Test: BMC becomes passive when sibling service is not running
 */
TEST_F(ManagerTest, BecomesPassive_SiblingServiceNotRunning)
{
    // Configure scenario: Sibling service is not running
    TestScenarioConfig config{.siblingServiceRunning = false};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();

    // Verify error log is created for passive due to error
    EXPECT_CALL(services, logError(errors::error_msg::bmcIsPassiveDueToError,
                                   errors::Level::Error, _))
        .Times(1);

    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", passiveTargetTimeout))
        .Times(1);

    RedundancyProps expectedProps{
        .role = Role::Passive,
        .redEnabled = false,
        .failoverInProgress = false,
        .failoversAllowed = false,
        .failoverImminent = false,
        .reasonsForNoRedundancy =
            {Redundancy::ReasonForNoRedundancy::SiblingCannotBeActive},
        .failoversNotAllowedReason = FailoversNotAllowedReason::None};

    setupPCIeStorageExpects(expectedProps);

    createManagerAndRun(ProgressPoint::passiveHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(expectedProps.role,
                         "Sibling BMC service is not running", true);
}

/**
 * @brief Test: BMC becomes passive when sibling is already active
 */
TEST_F(ManagerTest, BecomesPassive_SiblingAlreadyActive)
{
    // Configure scenario: Sibling is active, this BMC should be passive
    TestScenarioConfig config{.bmcPosition = 1, .siblingRole = Role::Active};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();

    // No errors
    EXPECT_CALL(services, logError(errors::error_msg::bmcIsPassiveDueToError,
                                   errors::Level::Error, _))
        .Times(0);

    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", passiveTargetTimeout))
        .Times(1);

    RedundancyProps expectedProps{
        .role = Role::Passive,
        .redEnabled = false,
        .failoverInProgress = false,
        .failoversAllowed = false,
        .failoverImminent = false,
        .reasonsForNoRedundancy = {},
        .failoversNotAllowedReason = FailoversNotAllowedReason::None};

    setupPCIeStorageExpects(expectedProps);

    createManagerAndRun(ProgressPoint::passiveHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(expectedProps.role, "Sibling is already active",
                         false);
}

/**
 * @brief Test: BMC becomes passive when sibling is already active, and
 *        redundancy is enabled.
 */
TEST_F(ManagerTest, BecomesPassive_SiblingAlreadyActive_RedEnabled)
{
    // Configure scenario: Sibling is active, this BMC should be passive
    TestScenarioConfig config{.bmcPosition = 1, .siblingRole = Role::Active};
    setupTestScenario(config);

    auto& sibling = mockProviders->getMockSibling();
    auto& services = mockProviders->getMockServices();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    // Sibling reports redundancy is enabled
    ON_CALL(sibling, getRedundancyEnabled())
        .WillByDefault(Return(std::make_optional(true)));

    ON_CALL(sibling, getFailoversAllowed())
        .WillByDefault(Return(std::make_optional(true)));

    // No errors
    EXPECT_CALL(services, logError(errors::error_msg::bmcIsPassiveDueToError,
                                   errors::Level::Error, _))
        .Times(0);

    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", passiveTargetTimeout))
        .Times(1);

    // A full sync should be run since redundancy is enabled
    EXPECT_CALL(syncInterface, doFullSync()).Times(1);

    createManagerAndRun(ProgressPoint::passiveHandlerStartComplete);

    RedundancyProps expectedProps{
        .role = Role::Passive,
        .redEnabled = true,
        .failoverInProgress = false,
        .failoversAllowed = true,
        .failoverImminent = false,
        .reasonsForNoRedundancy = {},
        .failoversNotAllowedReason = FailoversNotAllowedReason::None};

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(expectedProps.role, "Sibling is already active",
                         false);
}

/**
 * @brief Test: Manager reads previous role from persistent storage on startup
 */
TEST_F(ManagerTest, ReadsPreviousRole_OnStartup)
{
    using namespace std::chrono_literals;

    // Write previous role to persistent storage before creating Manager
    data::write(data::key::role, Role::Passive);

    // Configure scenario: Sibling role is Unknown so role determination
    // falls through to resumePrevious logic
    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Unknown};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();

    // No errors
    EXPECT_CALL(services, logError(errors::error_msg::bmcIsPassiveDueToError,
                                   errors::Level::Error, _))
        .Times(0);

    // Since previous role was Passive, manager should wait
    // for sibling role during startup
    EXPECT_CALL(sibling, waitForSiblingRole()).Times(1);

    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", passiveTargetTimeout))
        .Times(1);

    RedundancyProps expectedProps{
        .role = Role::Passive,
        .redEnabled = false,
        .failoverInProgress = false,
        .failoversAllowed = false,
        .failoverImminent = false,
        .reasonsForNoRedundancy = {},
        .failoversNotAllowedReason = FailoversNotAllowedReason::None};

    setupPCIeStorageExpects(expectedProps);

    createManagerAndRun(ProgressPoint::passiveHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(expectedProps.role, "Resuming previous role", false);
}
