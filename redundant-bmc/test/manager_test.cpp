// SPDX-License-Identifier: Apache-2.0
#include "manager.hpp"
#include "mocks/async_helpers.hpp"
#include "mocks/mock_providers.hpp"
#include "persistent_data_test_fixture.hpp"
#include "util.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace rbmc;
using namespace rbmc::util;
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
        Role siblingRole = Role::Unknown;
        bool siblingPaired = true;
        bool siblingFailoverInProgress = false;
        bool inLabMode = false;
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

        ON_CALL(services, isInSingleBMCLabMode())
            .WillByDefault(Return(config.inLabMode));
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

    static const inline RedundancyProps activeRedundancyEnabledProps{
        .role = Role::Active,
        .redEnabled = true,
        .failoverInProgress = false,
        .failoversAllowed = true,
        .failoverImminent = false,
        .reasonsForNoRedundancy = {},
        .failoversNotAllowedReason = FailoversNotAllowedReason::None};

    static RedundancyProps activeRedundancyDisabledProps(
        std::vector<Redundancy::ReasonForNoRedundancy> reasons)
    {
        return RedundancyProps{
            .role = Role::Active,
            .redEnabled = false,
            .failoverInProgress = false,
            .failoversAllowed = false,
            .failoverImminent = false,
            .reasonsForNoRedundancy = std::move(reasons),
            .failoversNotAllowedReason =
                FailoversNotAllowedReason::NoRedundancy};
    }

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

        // The constructor initializes PCIe storage with false; if the final
        // expected value differs, expect the initial false call as well.
        // if (vals.redEnabled)
        // {
        //     EXPECT_CALL(storage, updateRedundancyEnabled(false)).Times(1);
        // }
        // EXPECT_CALL(storage,
        // updateRedundancyEnabled(vals.redEnabled)).Times(1);
        //
        // EXPECT_CALL(storage,
        // updateFailoverInProgress(vals.failoverInProgress));
        //
        // if (vals.failoversAllowed)
        // {
        //     EXPECT_CALL(storage, updateFailoversAllowed(false)).Times(1);
        // }
        // EXPECT_CALL(storage, updateFailoversAllowed(vals.failoversAllowed))
        //     .Times(1);
    }

    /**
     * @brief Set the standard EXPECT_CALLs for when the BMC is active
     *        and the sibling is alive.
     */
    void setupActiveBMCWithAliveSiblingExpects()
    {
        auto& services = mockProviders->getMockServices();
        auto& sibling = mockProviders->getMockSibling();

        EXPECT_CALL(services, acquireFullHardwareAccess()).Times(1);
        EXPECT_CALL(services,
                    startUnit("obmc-bmc-active.target", activeTargetTimeout))
            .Times(1);
        EXPECT_CALL(sibling, waitForSiblingRole()).Times(1);
        EXPECT_CALL(sibling, waitForBMCSteadyState()).Times(1);
        EXPECT_CALL(services, waitForPeerConnection(_)).Times(1);
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

        // PassiveError only exists on the passive BMC
        if (expectedRole == Role::Passive)
        {
            auto savedPassiveError = data::read<bool>(data::key::passiveError);
            ASSERT_TRUE(savedPassiveError.has_value())
                << "PassiveError flag should be saved to persistent data";
            EXPECT_EQ(savedPassiveError.value(), expectPassiveError);
        }

        // Verify role reason description was saved
        auto savedRoleReason = data::read<std::string>(data::key::roleReason);
        ASSERT_TRUE(savedRoleReason.has_value())
            << "RoleReason should be saved to persistent data";
        EXPECT_EQ(savedRoleReason.value(), expectedRoleReason);
    }

    template <typename F>
    void spawnFunc(F func, sdbusplus::async::context& ctx)
    {
        ctx.spawn(func());
        ctx.run();
    }

    std::unique_ptr<MockProviders> mockProviders;
    std::unique_ptr<Manager> manager;
    std::string siblingServiceName;
    std::chrono::seconds passiveTargetTimeout{300};
    std::chrono::seconds activeTargetTimeout{1800};
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
 * @brief Test: BMC becomes active when not paired but in single BMC lab mode
 *        with no sibling present.
 */
TEST_F(ManagerTest, BecomesActive_LabMode_NoSibling)
{
    TestScenarioConfig config{.paired = false,
                              .siblingPresent = false,
                              .siblingAlive = false,
                              .siblingRole = Role::Unknown,
                              .inLabMode = true};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();

    EXPECT_CALL(services, logError(errors::error_msg::bmcIsPassiveDueToError,
                                   errors::Level::Error, _))
        .Times(0);
    EXPECT_CALL(sibling, waitForSiblingUp()).Times(0);
    EXPECT_CALL(sibling, waitForSiblingRole()).Times(0);
    EXPECT_CALL(services, waitForSelfPairing()).Times(0);
    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(1);
    EXPECT_CALL(services,
                startUnit("obmc-bmc-active.target", activeTargetTimeout))
        .Times(1);

    const auto expectedProps = activeRedundancyDisabledProps(
        {Redundancy::ReasonForNoRedundancy::SiblingMissing});
    setupPCIeStorageExpects(expectedProps);

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(Role::Active, "Sibling not alive", false);
}

/**
 * @brief Test: BMC becomes passive when not paired, in lab mode, but a
 *        sibling BMC is physically present.
 */
TEST_F(ManagerTest, BecomesPassive_LabMode_SiblingPresent)
{
    TestScenarioConfig config{
        .paired = false, .siblingPresent = true, .inLabMode = true};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    EXPECT_CALL(services, waitForSelfPairing()).Times(1);
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

/**
 * @brief Test: BMC becomes passive when position is 1
 */
TEST_F(ManagerTest, BecomesPassive_Position1)
{
    // Configure scenario: Sibling is alive, this BMC is position 1
    TestScenarioConfig config{.bmcPosition = 1, .siblingRole = Role::Unknown};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();

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
    verifyPersistentData(expectedProps.role, "BMC is not position 0", false);
}

/**
 * @brief Test: BMC becomes active when position is 0
 */
TEST_F(ManagerTest, BecomesActive_Position0)
{
    // Configure scenario: Sibling is alive, this BMC is position 0
    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Unknown};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();

    EXPECT_CALL(services,
                startUnit("obmc-bmc-active.target", activeTargetTimeout))
        .Times(1);

    RedundancyProps expectedProps{
        .role = Role::Active,
        .redEnabled = false,
        .failoverInProgress = false,
        .failoversAllowed = false,
        .failoverImminent = false,
        // This testcase only tests role selection, so not mocking the sibling
        // changing to passive and other things to enable redundancy.
        .reasonsForNoRedundancy =
            {Redundancy::ReasonForNoRedundancy::SiblingNotPassive},
        .failoversNotAllowedReason = FailoversNotAllowedReason::NoRedundancy};

    setupPCIeStorageExpects(expectedProps);

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(expectedProps.role, "BMC is position 0", false);
}

/**
 * @brief Test: BMC becomes active when no sibling present
 */
TEST_F(ManagerTest, BecomesActive_NoSibling)
{
    // Configure scenario: Sibling is not present
    TestScenarioConfig config{.bmcPosition = 1,
                              .siblingPresent = false,
                              .siblingAlive = false,
                              .siblingRole = Role::Unknown};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();

    // The wait functions shouldn't be called
    EXPECT_CALL(sibling, waitForSiblingUp()).Times(0);
    EXPECT_CALL(sibling, waitForSiblingRole()).Times(0);
    EXPECT_CALL(sibling, waitForBMCSteadyState()).Times(0);
    EXPECT_CALL(services, waitForPeerConnection(_)).Times(0);

    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(1);
    EXPECT_CALL(services,
                startUnit("obmc-bmc-active.target", activeTargetTimeout))
        .Times(1);

    const auto expectedProps = activeRedundancyDisabledProps(
        {Redundancy::ReasonForNoRedundancy::SiblingMissing});

    setupPCIeStorageExpects(expectedProps);

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(Role::Active, "Sibling not alive", false);
}

/**
 * @brief Test: BMC becomes active when sibling is dead
 */
TEST_F(ManagerTest, BecomesActive_SiblingDead)
{
    // Configure scenario: Sibling present but not alive
    TestScenarioConfig config{
        .bmcPosition = 1, .siblingPresent = true, .siblingAlive = false};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();

    // Only 1 wait function should be called
    EXPECT_CALL(sibling, waitForSiblingUp()).Times(1);
    EXPECT_CALL(sibling, waitForSiblingRole()).Times(0);
    EXPECT_CALL(sibling, waitForBMCSteadyState()).Times(0);
    EXPECT_CALL(services, waitForPeerConnection(_)).Times(0);

    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(1);
    EXPECT_CALL(services,
                startUnit("obmc-bmc-active.target", activeTargetTimeout))
        .Times(1);

    const auto expectedProps = activeRedundancyDisabledProps(
        {Redundancy::ReasonForNoRedundancy::SiblingNotAlive});

    setupPCIeStorageExpects(expectedProps);

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(Role::Active, "Sibling not alive", false);
}

/**
 * @brief Test: BMC becomes active and enables redundancy
 */
TEST_F(ManagerTest, BecomeActive_EnableRedundancy)
{
    // Configure scenario: This BMC should become active
    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    setupActiveBMCWithAliveSiblingExpects();

    EXPECT_CALL(services, logError(_, _, _)).Times(0);
    EXPECT_CALL(syncInterface, doFullSync()).Times(1);

    setupPCIeStorageExpects(activeRedundancyEnabledProps);

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(),
                          activeRedundancyEnabledProps);
    verifyPersistentData(Role::Active, "Sibling is already passive", false);
}

/**
 * @brief Test: BMC becomes active with redundancy enable but failovers
 *        not allowed due to the system booting.
 */
TEST_F(ManagerTest, BecomeActive_FailoversNotAllowed_SystemBooting)
{
    // Configure scenario: This BMC should become active
    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();

    setupActiveBMCWithAliveSiblingExpects();

    // Configure system state to be 'booting'
    ON_CALL(services, getSystemState())
        .WillByDefault(Return(SystemState::booting));

    RedundancyProps expectedProps{
        .role = Role::Active,
        .redEnabled = true,
        .failoverInProgress = false,
        .failoversAllowed = false,
        .failoverImminent = false,
        .reasonsForNoRedundancy = {},
        .failoversNotAllowedReason =
            FailoversNotAllowedReason::WrongSystemState};

    setupPCIeStorageExpects(expectedProps);

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(expectedProps.role, "Sibling is already passive",
                         false);
}

/**
 * @brief Test: BMC becomes active but disables redundancy
 *        due to full sync failure
 */
TEST_F(ManagerTest, BecomeActive_FullSyncFails_RedundancyDisabled)
{
    // Configure scenario: This BMC should become active
    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    setupActiveBMCWithAliveSiblingExpects();

    // Configure full sync to fail
    ON_CALL(syncInterface, doFullSync()).WillByDefault([]() {
        return test_helpers::makeCompletedTask(false);
    });

    // Expect full sync to be called and fail
    EXPECT_CALL(syncInterface, doFullSync()).Times(1);

    // Expect disableBackgroundSync to be called when redundancy is disabled
    EXPECT_CALL(syncInterface, disableBackgroundSync()).Times(1);

    // Expect error log to be created when redundancy can't be enabled
    EXPECT_CALL(services, logError(errors::error_msg::noRedundancy,
                                   errors::Level::Error, _))
        .Times(1);

    const auto expectedProps = activeRedundancyDisabledProps(
        {Redundancy::ReasonForNoRedundancy::DataSyncFailed});

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(expectedProps.role, "Sibling is already passive",
                         false);
}

/**
 * @brief Test: BMC becomes active but redundancy not enabled due to no
 *        peer connection
 */
TEST_F(ManagerTest, BecomeActive_PeerConnectionNeverConnects_RedundancyDisabled)
{
    // Configure scenario: This BMC should become active
    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    setupActiveBMCWithAliveSiblingExpects();

    // Configure getPeerConnected to always return false
    ON_CALL(services, getPeerConnected()).WillByDefault(Return(false));

    // Full sync should not be called.
    EXPECT_CALL(syncInterface, doFullSync()).Times(0);
    EXPECT_CALL(syncInterface, disableBackgroundSync()).Times(1);

    EXPECT_CALL(services, logError(errors::error_msg::noRedundancy,
                                   errors::Level::Error, _))
        .Times(1);

    const auto expectedProps = activeRedundancyDisabledProps(
        {Redundancy::ReasonForNoRedundancy::NetworkError});

    setupPCIeStorageExpects(expectedProps);

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(Role::Active, "Sibling is already passive", false);
}

/**
 * @brief Test: Redundancy is disabled because a passive BMC hardware
 *              problem external redundancy input is set.
 */
TEST_F(ManagerTest, BecomeActive_PassiveHWProblem_RedundancyDisabled)
{
    // Set the external redundancy input value used in the checks.
    util::writeExternalRedundancyInput(
        RedundancyInput::PassiveBMCHardwareProblem, true);

    // Configure scenario: This BMC should become active
    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    setupActiveBMCWithAliveSiblingExpects();

    // Full sync should not be called since redundancy is disabled
    EXPECT_CALL(syncInterface, doFullSync()).Times(0);
    EXPECT_CALL(syncInterface, disableBackgroundSync()).Times(1);

    EXPECT_CALL(services, logError(errors::error_msg::noRedundancy,
                                   errors::Level::Error, _))
        .Times(1);

    const auto expectedProps = activeRedundancyDisabledProps(
        {Redundancy::ReasonForNoRedundancy::SystemHWConfigIssue});

    setupPCIeStorageExpects(expectedProps);

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(Role::Active, "Sibling is already passive", false);
}

/**
 * @brief Test: Redundancy is disabled because redundancy was off when the
 *              system previously entered runtime state.
 */
TEST_F(ManagerTest, BecomeActive_RedundancyOffAtRuntimeStart_RedundancyDisabled)
{
    // Set redundancy off at runtime.  The tuple is {valid, off}.
    data::write(data::key::redundancyOffAtRuntime,
                std::tuple<bool, bool>{true, true});

    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    // System is at runtime.
    ON_CALL(services, getSystemState())
        .WillByDefault(Return(SystemState::runtime));

    setupActiveBMCWithAliveSiblingExpects();

    // Redundancy is disabled before sync is attempted
    EXPECT_CALL(syncInterface, doFullSync()).Times(0);
    EXPECT_CALL(syncInterface, disableBackgroundSync()).Times(1);

    EXPECT_CALL(services, logError(errors::error_msg::noRedundancy,
                                   errors::Level::Error, _))
        .Times(1);

    const auto expectedProps = activeRedundancyDisabledProps(
        {Redundancy::ReasonForNoRedundancy::RedundancyOffAtRuntimeStart});

    setupPCIeStorageExpects(expectedProps);

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(Role::Active, "Sibling is already passive", false);
}

/**
 * @brief Test: Redundancy disabled due to code version mismatch, with no
 *              code update in progress.
 */
TEST_F(ManagerTest, NoCodeUpdate_ErrorLogOnVersionMismatch)
{
    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();

    setupActiveBMCWithAliveSiblingExpects();

    // Sibling has a different FW version
    ON_CALL(sibling, getFWVersion())
        .WillByDefault(Return(std::make_optional<std::string>("AAAAAAAA")));

    // There should be an error log
    EXPECT_CALL(services, logError(errors::error_msg::noRedundancy,
                                   errors::Level::Error, _))
        .Times(1);

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);
}

/**
 * @brief Test: Redundancy disabled due to code version mismatch, but a code
 *              update is in progress. The error should not be logged when
 *              CodeVersionMismatch is the sole reason and a code update is
 *              in progress.
 */
TEST_F(ManagerTest, CodeUpdateInProgress_NoErrorLogOnVersionMismatch)
{
    // Mark a code update as in progress before the manager starts
    data::write(data::key::codeUpdateInProgress, true);

    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();

    setupActiveBMCWithAliveSiblingExpects();

    // Sibling has a different FW version — as expected during a code update
    ON_CALL(sibling, getFWVersion())
        .WillByDefault(Return(std::make_optional<std::string>("AAAAAAAA")));

    // With a code update in progress, the error log must NOT be emitted
    EXPECT_CALL(services, logError(_, _, _)).Times(0);

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    const auto expectedProps = activeRedundancyDisabledProps(
        {Redundancy::ReasonForNoRedundancy::CodeVersionMismatch});

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);

    // Code update is still in progress since the mismatch hasn't been resolved
    EXPECT_TRUE(manager->getCodeUpdateActivation().codeUpdateInProgress());
}

/**
 * @brief Test: A code update is in progress but redundancy is disabled for
 *              a reason other than CodeVersionMismatch (here: no peer
 *              connection).  CodeUpdateInProgress should get cleared
 **/
TEST_F(ManagerTest,
       CodeUpdateInProgress_ClearedWhenDisabledForNonMismatchReason)
{
    // Mark a code update as in progress before the manager starts
    data::write(data::key::codeUpdateInProgress, true);

    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();

    setupActiveBMCWithAliveSiblingExpects();

    // Peer not connected so redundancy can't be enabled.
    ON_CALL(services, getPeerConnected()).WillByDefault(Return(false));

    // Should be an error log.
    EXPECT_CALL(services, logError(errors::error_msg::noRedundancy,
                                   errors::Level::Error, _))
        .Times(1);

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    // Check these to verify NetworkError is only reason.
    const auto expectedProps = activeRedundancyDisabledProps(
        {Redundancy::ReasonForNoRedundancy::NetworkError});

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);

    // Code update no longer in progress
    EXPECT_FALSE(manager->getCodeUpdateActivation().codeUpdateInProgress());
}

/**
 * @brief Test: A code update was in progress on startup, but by the time
 *              redundancy is evaluated both BMCs have the same version.
 *              The code update in progress indication must be cleared.
 */
TEST_F(ManagerTest, CodeUpdateInProgress_ClearedWhenVersionsMatch)
{
    // Set a code update as in progress before the manager starts
    data::write(data::key::codeUpdateInProgress, true);

    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    setupActiveBMCWithAliveSiblingExpects();

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    // Code update in progress must have been cleared since versions matched
    // and redundancy was successfully enabled
    EXPECT_FALSE(manager->getCodeUpdateActivation().codeUpdateInProgress());
}

/**
 * @brief Test: When the system transitions to booting while a code update
 *              is in progress, the code update in progress indication is
 *              cleared.
 */
TEST_F(ManagerTest, CodeUpdateClearedOnBoot)
{
    // Mark a code update as in progress before the manager starts
    data::write(data::key::codeUpdateInProgress, true);

    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();

    setupActiveBMCWithAliveSiblingExpects();

    // Sibling has a different FW version so the code update flag survives
    // startup.
    ON_CALL(sibling, getFWVersion())
        .WillByDefault(Return(std::make_optional<std::string>("AAAAAAAA")));

    // Error log suppressed since code update is in progress
    EXPECT_CALL(services, logError(_, _, _)).Times(0);

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    // Confirm code update is still flagged after startup
    ASSERT_TRUE(manager->getCodeUpdateActivation().codeUpdateInProgress())
        << "Code update should still be in progress";

    // Simulate the system transitioning to booting.
    services.runSystemStateCallback(Role::Active, SystemState::booting);

    // The booting transition should have cleared the code update indication
    EXPECT_FALSE(manager->getCodeUpdateActivation().codeUpdateInProgress());
}

/**
 * @brief Test: The code update callback correctly sets and clears the
 *             code-update-in-progress indication.
 *
 * Runs the callback with started=true to verify the flag is set, then
 * with started=false (failure) to verify it is cleared.
 */
TEST_F(ManagerTest, CodeUpdateCallback_SetsAndClearsInProgress)
{
    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    setupActiveBMCWithAliveSiblingExpects();

    auto& services = mockProviders->getMockServices();

    createManagerAndRun(ProgressPoint::activeHandlerStartComplete);

    // Confirm flag is clear before a code update starts
    ASSERT_FALSE(manager->getCodeUpdateActivation().codeUpdateInProgress());

    // Simulate a code update starting
    services.runCodeUpdateCallback(Role::Active, true);

    EXPECT_TRUE(manager->getCodeUpdateActivation().codeUpdateInProgress())
        << "codeUpdateInProgress should be true";

    // Simulate the update failing
    services.runCodeUpdateCallback(Role::Active, false);

    EXPECT_FALSE(manager->getCodeUpdateActivation().codeUpdateInProgress())
        << "codeUpdateInProgress should be false";
}

/**
 * @brief Test: Passive BMC successfully fails over and becomes Active
 */
TEST_F(ManagerTest, StartFailover_SuccessfulFailoverToActive)
{
    TestScenarioConfig config{.bmcPosition = 1, .siblingRole = Role::Active};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();
    auto& siblingReset = mockProviders->getMockSiblingReset();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    // The active BMC sibling has redundancy enabled and failovers allowed
    ON_CALL(sibling, getRedundancyEnabled())
        .WillByDefault(Return(std::optional<bool>(true)));
    ON_CALL(sibling, getFailoversAllowed())
        .WillByDefault(Return(std::optional<bool>(true)));

    // The sibling role starts as Active and changes to Passive when
    // the sibling is reset during the failover.
    Role siblingRole = Role::Active;
    ON_CALL(sibling, getRole()).WillByDefault([&siblingRole]() {
        return std::optional<Role>(siblingRole);
    });

    // First the passive target starts, then the active
    // on during the failover.
    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", passiveTargetTimeout))
        .Times(1);
    EXPECT_CALL(services,
                startUnit("obmc-bmc-active.target", activeTargetTimeout))
        .Times(1);

    // Background sync disabled (called during passive startup and failover)
    EXPECT_CALL(syncInterface, disableBackgroundSync()).Times(AtLeast(1));

    // Failover imminent delay
    EXPECT_CALL(services, doFailoverImminentDelay()).Times(1);

    // The sibling is reset; its role then reports as Passive
    EXPECT_CALL(siblingReset, toggleReset()).WillOnce([&siblingRole]() {
        siblingRole = Role::Passive;
        return test_helpers::makeCompletedTask();
    });

    // Hardware access is acquired
    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(1);

    // The failover started error should be logged
    EXPECT_CALL(services, logError(errors::error_msg::failoverStarted,
                                   errors::Level::Informational, _))
        .Times(1);

    manager =
        std::make_unique<Manager>(ctx, std::move(mockProviders), hbInterval);

    // Run to starting the passive role handler, then request the failover,
    // then wait for the progress point saying it's complete.
    spawnFunc(
        [this]() -> sdbusplus::async::task<> {
            co_await waitForProgressPoint(
                ProgressPoint::passiveHandlerStartComplete, false);

            FailoverOptions options;
            co_await manager->method_call(Manager::start_failover_t{},
                                          Requester::Host, options);

            co_await waitForProgressPoint(ProgressPoint::failoverComplete);
        },
        ctx);

    // This BMC now reports its active with redundancy enabled.
    verifyRedundancyProps(manager->getRedundancyInterface(),
                          activeRedundancyEnabledProps);

    verifyPersistentData(Role::Active, "Failover", false);
}

/**
 * @brief Test: Passive BMC with redundancy enabled tries to
 *        failover but is blocked because failovers are not allowed.
 */
TEST_F(ManagerTest, FailoverBlocked_NotAllowed)
{
    TestScenarioConfig config{.bmcPosition = 1, .siblingRole = Role::Active};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();
    auto& siblingReset = mockProviders->getMockSiblingReset();

    // The active BMC sibling has redundancy enabled
    // and failovers not allowed
    ON_CALL(sibling, getRedundancyEnabled())
        .WillByDefault(Return(std::optional<bool>(true)));
    ON_CALL(sibling, getFailoversAllowed())
        .WillByDefault(Return(std::optional<bool>(false)));

    // Expect error log for blocked failover
    EXPECT_CALL(services, logError(errors::error_msg::failoverBlocked,
                                   errors::Level::Warning, _))
        .Times(1);

    // Failover should not proceed, so these should not be called
    EXPECT_CALL(services, doFailoverImminentDelay()).Times(0);
    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(0);
    EXPECT_CALL(siblingReset, toggleReset()).Times(0);

    manager =
        std::make_unique<Manager>(ctx, std::move(mockProviders), hbInterval);

    spawnFunc(
        [this]() -> sdbusplus::async::task<> {
            using namespace std::chrono_literals;
            // Wait for startup to complete
            co_await waitForProgressPoint(
                ProgressPoint::passiveHandlerStartComplete, false);

            // StartFailover should throw because failovers aren't allowed.
            try
            {
                FailoverOptions options;
                co_await manager->method_call(Manager::start_failover_t{},
                                              Requester::Host, options);
                ADD_FAILURE() << "StartFailover should not have have succeeded";
            }
            catch (const sdbusplus::xyz::openbmc_project::Common::Error::
                       Unavailable&)
            {}

            ctx.request_stop();
        },
        ctx);

    // Still passive
    RedundancyProps expectedProps{
        .role = Role::Passive,
        .redEnabled = true,
        .failoverInProgress = false,
        .failoversAllowed = false,
        .failoverImminent = false,
        .reasonsForNoRedundancy = {},
        .failoversNotAllowedReason = FailoversNotAllowedReason::None};

    const auto& redInterface = manager->getRedundancyInterface();

    verifyRedundancyProps(redInterface, expectedProps);

    verifyPersistentData(expectedProps.role, "Sibling is already active",
                         false);
}

/**
 * @brief Test: Passive BMC with redundancy enabled tries to failover,
 *        failovers are not allowed, but the forced failover option
 *        overrides the block and the failover succeeds.
 */
TEST_F(ManagerTest, FailoversNotAllowed_ForcedSucceeds)
{
    TestScenarioConfig config{.bmcPosition = 1, .siblingRole = Role::Active};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();
    auto& siblingReset = mockProviders->getMockSiblingReset();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    // The active BMC sibling has redundancy enabled
    // and failovers not allowed
    ON_CALL(sibling, getRedundancyEnabled())
        .WillByDefault(Return(std::optional<bool>(true)));
    ON_CALL(sibling, getFailoversAllowed())
        .WillByDefault(Return(std::optional<bool>(false)));

    // The sibling role starts as Active and changes to Passive when
    // the sibling is reset during the failover.
    Role siblingRole = Role::Active;
    ON_CALL(sibling, getRole()).WillByDefault([&siblingRole]() {
        return std::optional<Role>(siblingRole);
    });

    // First the passive target starts, then the active on during the failover.
    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", passiveTargetTimeout))
        .Times(1);
    EXPECT_CALL(services,
                startUnit("obmc-bmc-active.target", activeTargetTimeout))
        .Times(1);

    EXPECT_CALL(syncInterface, disableBackgroundSync()).Times(AtLeast(1));
    EXPECT_CALL(services, doFailoverImminentDelay()).Times(1);
    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(1);

    // The failover started error should be logged
    EXPECT_CALL(services, logError(errors::error_msg::failoverStarted,
                                   errors::Level::Informational, _))
        .Times(1);

    // The sibling is reset; its role then reports as Passive after that
    EXPECT_CALL(siblingReset, toggleReset()).WillOnce([&siblingRole]() {
        siblingRole = Role::Passive;
        return test_helpers::makeCompletedTask();
    });

    manager =
        std::make_unique<Manager>(ctx, std::move(mockProviders), hbInterval);

    spawnFunc(
        [this]() -> sdbusplus::async::task<> {
            co_await waitForProgressPoint(
                ProgressPoint::passiveHandlerStartComplete, false);

            using Failover =
                sdbusplus::common::xyz::openbmc_project::control::Failover;
            FailoverOptions options;
            options.emplace(
                Failover::convertOptionsToString(Failover::Options::Force),
                true);

            co_await manager->method_call(Manager::start_failover_t{},
                                          Requester::Host, options);

            co_await waitForProgressPoint(ProgressPoint::failoverComplete);
        },
        ctx);

    verifyRedundancyProps(manager->getRedundancyInterface(),
                          activeRedundancyEnabledProps);

    verifyPersistentData(Role::Active, "Failover", false);
}

/**
 * @brief Test: Active BMC successfully forwards a failover request to the
 *        passive sibling.
 */
TEST_F(ManagerTest, StartFailoverOnActive)
{
    // This BMC is active with a passive sibling
    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    auto& sibling = mockProviders->getMockSibling();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    setupActiveBMCWithAliveSiblingExpects();
    EXPECT_CALL(syncInterface, doFullSync()).Times(1);

    // The failover request should be forwarded to the passive sibling
    EXPECT_CALL(sibling, startFailover(Requester::Host, _))
        .WillOnce([](auto /*requester*/, const auto& /*options*/) {
            return test_helpers::makeCompletedTask();
        });

    manager =
        std::make_unique<Manager>(ctx, std::move(mockProviders), hbInterval);

    // After the active start is complete, call startFailover.
    spawnFunc(
        [this]() -> sdbusplus::async::task<> {
            co_await waitForProgressPoint(
                ProgressPoint::activeHandlerStartComplete, false);

            FailoverOptions options;
            co_await manager->method_call(Manager::start_failover_t{},
                                          Requester::Host, options);

            // The active BMC now waits for a reset that won't come in tests.
            ctx.request_stop();
        },
        ctx);

    RedundancyProps expectedProps{
        .role = Role::Active,
        .redEnabled = true,
        .failoverInProgress = false,
        .failoversAllowed = true,
        .failoverImminent = false,
        .reasonsForNoRedundancy = {},
        .failoversNotAllowedReason = FailoversNotAllowedReason::None};

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
}
/**
 * @brief Test: Active BMC forwards a failover request to the passive sibling
 *        with a UseRedundancyInput option, and verifies the option is
 *        passed through unchanged to the sibling.
 */
TEST_F(ManagerTest, StartFailoverOnActive_WithUseRedundancyInput)
{
    // This BMC is active with a passive sibling
    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    auto& sibling = mockProviders->getMockSibling();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    setupActiveBMCWithAliveSiblingExpects();
    EXPECT_CALL(syncInterface, doFullSync()).Times(1);

    using Failover = sdbusplus::common::xyz::openbmc_project::control::Failover;
    using Redundancy =
        sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy;

    const std::string expectedKey =
        Failover::convertOptionsToString(Failover::Options::UseRedundancyInput);
    const std::string expectedValue =
        Redundancy::convertRedundancyInputToString(
            Redundancy::RedundancyInput::PassiveBMCHardwareProblem);

    // The failover request should be forwarded to the passive sibling with
    // the UseRedundancyInput option intact.
    EXPECT_CALL(sibling,
                startFailover(Requester::Host,
                              Contains(Pair(expectedKey,
                                            FailoverOptions::mapped_type{
                                                expectedValue}))))
        .WillOnce([](auto /*requester*/, const auto& /*opts*/) {
            return test_helpers::makeCompletedTask();
        });

    manager =
        std::make_unique<Manager>(ctx, std::move(mockProviders), hbInterval);

    spawnFunc(
        [this, &expectedKey, &expectedValue]() -> sdbusplus::async::task<> {
            co_await waitForProgressPoint(
                ProgressPoint::activeHandlerStartComplete, false);

            FailoverOptions options;
            options.emplace(expectedKey, expectedValue);

            co_await manager->method_call(Manager::start_failover_t{},
                                          Requester::Host, options);

            // The active BMC now waits for a reset that won't come in tests.
            ctx.request_stop();
        },
        ctx);
}

/**
 * @brief Test: Active BMC blocks a failover request because the system
 *        is still booting and failovers are not allowed.
 */
TEST_F(ManagerTest, StartFailover_ActiveBlocked_FailoversNotAllowed)
{
    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    // System is booting which will turn off failoversAllowed
    ON_CALL(services, getSystemState())
        .WillByDefault(Return(SystemState::booting));

    setupActiveBMCWithAliveSiblingExpects();
    EXPECT_CALL(syncInterface, doFullSync()).Times(1);

    // Failover should be blocked so shouldn't call startFailover
    EXPECT_CALL(sibling, startFailover(_, _)).Times(0);

    EXPECT_CALL(services, logError(errors::error_msg::failoverBlocked,
                                   errors::Level::Warning, _))
        .Times(1);

    manager =
        std::make_unique<Manager>(ctx, std::move(mockProviders), hbInterval);

    spawnFunc(
        [this]() -> sdbusplus::async::task<> {
            co_await waitForProgressPoint(
                ProgressPoint::activeHandlerStartComplete, false);

            // The call should throw.
            try
            {
                FailoverOptions options;
                co_await manager->method_call(Manager::start_failover_t{},
                                              Requester::Host, options);
                ADD_FAILURE() << "StartFailover should not have succeeded";
            }
            catch (const sdbusplus::xyz::openbmc_project::Common::Error::
                       Unavailable&)
            {}

            ctx.request_stop();
        },
        ctx);

    RedundancyProps expectedProps{
        .role = Role::Active,
        .redEnabled = true,
        .failoverInProgress = false,
        .failoversAllowed = false,
        .failoverImminent = false,
        .reasonsForNoRedundancy = {},
        .failoversNotAllowedReason =
            FailoversNotAllowedReason::WrongSystemState};

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
}

/**
 * @brief Test: After a failover, the new passive (formerly active) BMC comes
 *              back in Quiesced state so redundancy is never enabled.
 */
TEST_F(ManagerTest, StartFailover_SiblingQuiesced_RedundancyNeverEnabled)
{
    TestScenarioConfig config{.bmcPosition = 1, .siblingRole = Role::Active};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();
    auto& siblingReset = mockProviders->getMockSiblingReset();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    // The active BMC sibling has redundancy enabled and failovers allowed
    ON_CALL(sibling, getRedundancyEnabled())
        .WillByDefault(Return(std::optional<bool>(true)));
    ON_CALL(sibling, getFailoversAllowed())
        .WillByDefault(Return(std::optional<bool>(true)));

    // The sibling role starts as Active
    Role siblingRole = Role::Active;
    ON_CALL(sibling, getRole()).WillByDefault([&siblingRole]() {
        return std::optional<Role>(siblingRole);
    });

    // After the reset the sibling returns Quiesced instead of Ready
    ON_CALL(sibling, getBMCState())
        .WillByDefault(Return(std::make_optional(BMCState::Quiesced)));

    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", passiveTargetTimeout))
        .Times(1);
    EXPECT_CALL(services,
                startUnit("obmc-bmc-active.target", activeTargetTimeout))
        .Times(1);

    EXPECT_CALL(syncInterface, disableBackgroundSync()).Times(AtLeast(1));
    EXPECT_CALL(services, doFailoverImminentDelay()).Times(1);

    EXPECT_CALL(siblingReset, toggleReset()).WillOnce([&siblingRole]() {
        siblingRole = Role::Passive;
        return test_helpers::makeCompletedTask();
    });

    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(1);

    EXPECT_CALL(services, logError(errors::error_msg::failoverStarted,
                                   errors::Level::Informational, _))
        .Times(1);

    // Redundancy disabled since sibling the new passive is Quiesced
    EXPECT_CALL(services, logError(errors::error_msg::noRedundancy,
                                   errors::Level::Error, _))
        .Times(1);

    manager =
        std::make_unique<Manager>(ctx, std::move(mockProviders), hbInterval);

    spawnFunc(
        [this]() -> sdbusplus::async::task<> {
            co_await waitForProgressPoint(
                ProgressPoint::passiveHandlerStartComplete, false);

            FailoverOptions options;
            co_await manager->method_call(Manager::start_failover_t{},
                                          Requester::Host, options);

            co_await waitForProgressPoint(ProgressPoint::failoverComplete);
        },
        ctx);

    // Redundancy was never enabled
    const auto expectedProps = activeRedundancyDisabledProps(
        {Redundancy::ReasonForNoRedundancy::SiblingNotAtReady});

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(Role::Active, "Failover", false);
}

/**
 * @brief Test: Passive BMC starts a failover with UseRedundancyInput set to
 *        PassiveBMCHardwareProblem.  After becoming active the input must be
 *        persisted in the data file, and redundancy must be disabled with the
 *        SystemHWConfigIssue reason.
 */
TEST_F(ManagerTest, StartFailover_WithUseRedundancyInput_DisablesRedundancy)
{
    TestScenarioConfig config{.bmcPosition = 1, .siblingRole = Role::Active};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();
    auto& siblingReset = mockProviders->getMockSiblingReset();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    ON_CALL(sibling, getRedundancyEnabled())
        .WillByDefault(Return(std::optional<bool>(true)));
    ON_CALL(sibling, getFailoversAllowed())
        .WillByDefault(Return(std::optional<bool>(true)));

    ON_CALL(sibling, getBMCState())
        .WillByDefault(Return(std::make_optional(BMCState::Ready)));

    // The sibling role starts as Active and changes to Passive on reset
    Role siblingRole = Role::Active;
    ON_CALL(sibling, getRole()).WillByDefault([&siblingRole]() {
        return std::optional<Role>(siblingRole);
    });

    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", passiveTargetTimeout))
        .Times(1);
    EXPECT_CALL(services,
                startUnit("obmc-bmc-active.target", activeTargetTimeout))
        .Times(1);

    EXPECT_CALL(syncInterface, disableBackgroundSync()).Times(AtLeast(1));
    EXPECT_CALL(services, doFailoverImminentDelay()).Times(1);

    EXPECT_CALL(siblingReset, toggleReset()).WillOnce([&siblingRole]() {
        siblingRole = Role::Passive;
        return test_helpers::makeCompletedTask();
    });

    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(1);

    EXPECT_CALL(services, logError(errors::error_msg::failoverStarted,
                                   errors::Level::Informational, _))
        .Times(1);

    // Redundancy must be disabled after the failover due to the HW issue input
    EXPECT_CALL(services, logError(errors::error_msg::noRedundancy,
                                   errors::Level::Error, _))
        .Times(1);

    manager =
        std::make_unique<Manager>(ctx, std::move(mockProviders), hbInterval);

    using Failover = sdbusplus::common::xyz::openbmc_project::control::Failover;

    const std::string redInputValue =
        Redundancy::convertRedundancyInputToString(
            Redundancy::RedundancyInput::PassiveBMCHardwareProblem);

    spawnFunc(
        [this, &redInputValue]() -> sdbusplus::async::task<> {
            co_await waitForProgressPoint(
                ProgressPoint::passiveHandlerStartComplete, false);

            // Add in the UseRedundancyInput = PassiveBMCHardwareProblem option
            FailoverOptions options;
            options.emplace(Failover::convertOptionsToString(
                                Failover::Options::UseRedundancyInput),
                            redInputValue);

            co_await manager->method_call(Manager::start_failover_t{},
                                          Requester::Host, options);

            co_await waitForProgressPoint(ProgressPoint::failoverComplete);
        },
        ctx);

    // Verify the redundancy input was persisted to the data file
    const auto savedInputs = readExternalRedundancyInputs();
    ASSERT_TRUE(savedInputs.contains(
        Redundancy::RedundancyInput::PassiveBMCHardwareProblem))
        << "PassiveBMCHardwareProblem should be persisted after failover";

    // Verify redundancy is disabled with the SystemHWConfigIssue reason
    const auto expectedProps = activeRedundancyDisabledProps(
        {Redundancy::ReasonForNoRedundancy::SystemHWConfigIssue});

    verifyRedundancyProps(manager->getRedundancyInterface(), expectedProps);
    verifyPersistentData(Role::Active, "Failover", false);
}
