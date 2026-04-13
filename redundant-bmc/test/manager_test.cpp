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
        bool provisioned = true;
        std::optional<size_t> bmcPosition = 0;
        bool systemInventoryAvailable = true;
        bool siblingServiceRunning = true;
        bool siblingPresent = true;
        bool siblingAlive = true;
        Role siblingRole = Role::Passive;
        bool siblingProvisioned = true;
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

        ON_CALL(services, getProvisioned())
            .WillByDefault(Return(config.provisioned));

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

        ON_CALL(sibling, getProvisioned())
            .WillByDefault(Return(config.siblingProvisioned));

        ON_CALL(sibling, getFailoverInProgress())
            .WillByDefault(Return(config.siblingFailoverInProgress));
    }

    /**
     * @brief Create the Manager and let it run until a event is hit.
     */
    void createManagerAndRun(Event waitEvent)
    {
        using namespace std::chrono_literals;

        // Spawns Manager::startup()
        manager = std::make_unique<Manager>(ctx, std::move(mockProviders),
                                            hbInterval);

        ctx.spawn(waitForEvent(waitEvent));

        ctx.run();
    }

    /**
     * @brief Let the context run until a event has occurred.
     *
     * @param[in] cp - The event to wait for
     * @param[in] timeout - The timeout, default is 1s
     * @param[in] stopWhenDone - If the context should be stopped afterwards.
     */
    sdbusplus::async::task<> waitForEvent(
        Event cp,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{1000},
        bool stopWhenDone = true)
    {
        using namespace std::chrono_literals;
        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (std::chrono::steady_clock::now() < deadline)
        {
            if (manager &&
                manager->getProviders().getEventRecorder().hasEvent(cp))
            {
                if (stopWhenDone)
                {
                    ctx.request_stop();
                }
                co_return;
            }
            co_await sdbusplus::async::sleep_for(ctx, 10ms);
        }

        ADD_FAILURE() << "Timed out waiting for event "
                      << std::to_underlying(cp);
        // If timed out, stop regardless
        ctx.request_stop();
    }

    template <typename F>
    static auto runFunc(F func) -> sdbusplus::async::task<>
    {
        co_await func();
    }

    template <typename F>
    void spawnFunc(F func, sdbusplus::async::context& ctx)
    {
        ctx.spawn(runFunc(std::move(func)));
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
    sdbusplus::async::context ctx;
    BMCState siblingState{BMCState::Ready};
};

/**
 * @brief Test: BMC becomes passive when sibling is already active
 */
TEST_F(ManagerTest, BecomesPassive_SiblingAlreadyActive)
{
    // Configure scenario: Sibling is active, this BMC should be passive
    TestScenarioConfig config{.bmcPosition = 1, .siblingRole = Role::Active};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();

    // Expect passive role handler to start the passive target
    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", std::chrono::seconds(300)))
        .Times(1);

    createManagerAndRun(Event::passiveHandlerStartComplete);

    ASSERT_NE(manager, nullptr);
    const auto& redInterface = manager->getRedundancyInterface();

    EXPECT_EQ(redInterface.role(), Role::Passive);
    EXPECT_FALSE(redInterface.redundancy_enabled());

    verifyPersistentData(Role::Passive, "Sibling is already active", false);
}

/**
 * @brief Test: BMC becomes passive when not provisioned
 */
TEST_F(ManagerTest, BecomesPassive_NotProvisioned)
{
    // Configure scenario: BMC is not provisioned
    TestScenarioConfig config{.provisioned = false};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();

    // Verify error log is created for passive due to error
    EXPECT_CALL(services, logError(errors::error_msg::bmcIsPassiveDueToError,
                                   errors::Level::Error, _))
        .Times(1);

    // Expect passive role handler to start the passive target
    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", std::chrono::seconds(300)))
        .Times(1);

    createManagerAndRun(Event::passiveHandlerStartComplete);

    ASSERT_NE(manager, nullptr);
    const auto& redInterface = manager->getRedundancyInterface();

    EXPECT_EQ(redInterface.role(), Role::Passive);
    EXPECT_FALSE(redInterface.redundancy_enabled());

    const auto& reasons = redInterface.reasons_for_no_redundancy();
    EXPECT_EQ(reasons.size(), 1);
    EXPECT_TRUE(std::ranges::contains(
        reasons, Redundancy::ReasonForNoRedundancy::SiblingCannotBeActive));

    verifyPersistentData(Role::Passive, "BMC is not provisioned", true);
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

    // Verify error log is created for passive due to error
    EXPECT_CALL(services, logError(errors::error_msg::bmcIsPassiveDueToError,
                                   errors::Level::Error, _))
        .Times(1);

    // Expect passive role handler to start the passive target
    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", std::chrono::seconds(300)))
        .Times(1);

    createManagerAndRun(Event::passiveHandlerStartComplete);

    ASSERT_NE(manager, nullptr);
    const auto& redInterface = manager->getRedundancyInterface();

    EXPECT_EQ(redInterface.role(), Role::Passive);
    EXPECT_FALSE(redInterface.redundancy_enabled());

    const auto& reasons = redInterface.reasons_for_no_redundancy();
    EXPECT_EQ(reasons.size(), 1);
    EXPECT_TRUE(std::ranges::contains(
        reasons, Redundancy::ReasonForNoRedundancy::SiblingCannotBeActive));

    verifyPersistentData(Role::Passive, "Cannot determine BMC position", true);
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

    // Expect passive role handler to start the passive target
    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", std::chrono::seconds(300)))
        .Times(1);

    createManagerAndRun(Event::passiveHandlerStartComplete);

    ASSERT_NE(manager, nullptr);
    const auto& redInterface = manager->getRedundancyInterface();

    EXPECT_EQ(redInterface.role(), Role::Passive);
    EXPECT_FALSE(redInterface.redundancy_enabled());

    const auto& reasons = redInterface.reasons_for_no_redundancy();
    EXPECT_EQ(reasons.size(), 1);
    EXPECT_TRUE(std::ranges::contains(
        reasons, Redundancy::ReasonForNoRedundancy::SiblingCannotBeActive));

    verifyPersistentData(Role::Passive, "System inventory is not available",
                         true);
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

    // Expect passive role handler to start the passive target
    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", std::chrono::seconds(300)))
        .Times(1);

    createManagerAndRun(Event::passiveHandlerStartComplete);

    ASSERT_NE(manager, nullptr);
    const auto& redInterface = manager->getRedundancyInterface();

    EXPECT_EQ(redInterface.role(), Role::Passive);
    EXPECT_FALSE(redInterface.redundancy_enabled());

    const auto& reasons = redInterface.reasons_for_no_redundancy();
    EXPECT_EQ(reasons.size(), 1);
    EXPECT_TRUE(std::ranges::contains(
        reasons, Redundancy::ReasonForNoRedundancy::SiblingCannotBeActive));

    verifyPersistentData(Role::Passive, "Sibling BMC service is not running",
                         true);
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

    // Since previous role was Passive, manager should wait
    // for sibling role during startup
    EXPECT_CALL(sibling, waitForSiblingRole()).Times(1);

    // Expect passive role handler to start the passive target with exact args
    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", std::chrono::seconds(300)))
        .Times(1);

    createManagerAndRun(Event::passiveHandlerStartComplete);

    ASSERT_NE(manager, nullptr);
    const auto& redInterface = manager->getRedundancyInterface();

    EXPECT_EQ(redInterface.role(), Role::Passive);

    verifyPersistentData(Role::Passive, "Resuming previous role", false);
}

/**
 * @brief Test: BMC doesn't wait for sibling when sibling is not present
 */
TEST_F(ManagerTest, NoWaitForSibling_WhenNotPresent)
{
    // Configure scenario: Sibling is not present
    TestScenarioConfig config{.bmcPosition = 0,
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
    EXPECT_CALL(services, waitForPeerConnection()).Times(0);

    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(1);
    EXPECT_CALL(services,
                startUnit("obmc-bmc-active.target", std::chrono::seconds(600)))
        .Times(1);

    createManagerAndRun(Event::activeHandlerStartComplete);

    ASSERT_NE(manager, nullptr);
    const auto& redInterface = manager->getRedundancyInterface();

    EXPECT_EQ(redInterface.role(), Role::Active);

    // Redundancy should not be enabled (no sibling present)
    EXPECT_FALSE(redInterface.redundancy_enabled());

    const auto& reasons = redInterface.reasons_for_no_redundancy();
    EXPECT_EQ(reasons.size(), 1);
    EXPECT_TRUE(std::ranges::contains(
        reasons, Redundancy::ReasonForNoRedundancy::SiblingMissing));

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
    auto& sibling = mockProviders->getMockSibling();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    // Configure firmware versions to match (required for redundancy)
    ON_CALL(services, getFWVersion()).WillByDefault(Return("12345678"));
    ON_CALL(sibling, getFWVersion())
        .WillByDefault(Return(std::optional<std::string>("12345678")));

    // Expect active role handler operations
    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(1);
    EXPECT_CALL(services,
                startUnit("obmc-bmc-active.target", std::chrono::seconds(600)))
        .Times(1);

    // Expect sibling wait operations (since sibling is alive)
    EXPECT_CALL(sibling, waitForSiblingRole()).Times(1);
    EXPECT_CALL(sibling, waitForBMCSteadyState()).Times(1);

    // Expect peer connection wait
    EXPECT_CALL(services, waitForPeerConnection()).Times(1);

    // Expect full sync to be called and succeed
    EXPECT_CALL(syncInterface, doFullSync()).Times(1);

    createManagerAndRun(Event::activeHandlerStartComplete);

    ASSERT_NE(manager, nullptr);
    const auto& redInterface = manager->getRedundancyInterface();

    EXPECT_EQ(redInterface.role(), Role::Active);

    // Verify redundancy is enabled after sync
    EXPECT_TRUE(redInterface.redundancy_enabled());
    EXPECT_TRUE(redInterface.reasons_for_no_redundancy().empty());

    // Verify failovers are allowed
    EXPECT_TRUE(redInterface.failovers_allowed());
    EXPECT_EQ(redInterface.failovers_not_allowed_reason(),
              Redundancy::FailoversNotAllowedReason::None);

    verifyPersistentData(Role::Active, "Sibling is already passive", false);
}

/**
 * @brief Test: BMC becomes active with redundancy enabled but failovers not
 * allowed due to system booting
 */
TEST_F(ManagerTest,
       BecomeActive_RedundancyEnabled_FailoversNotAllowed_SystemBooting)
{
    // Configure scenario: This BMC should become active
    TestScenarioConfig config{.bmcPosition = 0, .siblingRole = Role::Passive};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    // Configure system state to be 'booting'
    ON_CALL(services, getSystemState())
        .WillByDefault(Return(SystemState::booting));

    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(1);
    EXPECT_CALL(services,
                startUnit("obmc-bmc-active.target", std::chrono::seconds(600)))
        .Times(1);

    // Expect sibling wait operations (since sibling is alive)
    EXPECT_CALL(sibling, waitForSiblingRole()).Times(1);
    EXPECT_CALL(sibling, waitForBMCSteadyState()).Times(1);
    EXPECT_CALL(services, waitForPeerConnection()).Times(1);

    // Expect full sync to be called and succeed
    EXPECT_CALL(syncInterface, doFullSync()).Times(1);

    createManagerAndRun(Event::activeHandlerStartComplete);

    ASSERT_NE(manager, nullptr);
    const auto& redInterface = manager->getRedundancyInterface();

    EXPECT_EQ(redInterface.role(), Role::Active);

    EXPECT_TRUE(redInterface.redundancy_enabled());
    EXPECT_TRUE(redInterface.reasons_for_no_redundancy().empty());

    // Verify failovers are not allowed due to system state
    EXPECT_FALSE(redInterface.failovers_allowed());
    EXPECT_EQ(redInterface.failovers_not_allowed_reason(),
              Redundancy::FailoversNotAllowedReason::WrongSystemState);

    verifyPersistentData(Role::Active, "Sibling is already passive", false);
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
    auto& sibling = mockProviders->getMockSibling();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(1);
    EXPECT_CALL(services,
                startUnit("obmc-bmc-active.target", std::chrono::seconds(600)))
        .Times(1);

    // Expect sibling wait operations (since sibling is alive)
    EXPECT_CALL(sibling, waitForSiblingRole()).Times(1);
    EXPECT_CALL(sibling, waitForBMCSteadyState()).Times(1);
    EXPECT_CALL(services, waitForPeerConnection()).Times(1);

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

    createManagerAndRun(Event::activeHandlerStartComplete);

    ASSERT_NE(manager, nullptr);
    const auto& redInterface = manager->getRedundancyInterface();

    EXPECT_EQ(redInterface.role(), Role::Active);

    // Verify redundancy is disabled due to sync failure
    EXPECT_FALSE(redInterface.redundancy_enabled());

    // Verify the reason for no redundancy includes DataSyncFailed
    const auto& reasons = redInterface.reasons_for_no_redundancy();
    EXPECT_FALSE(reasons.empty());
    EXPECT_TRUE(std::ranges::contains(
        reasons, Redundancy::ReasonForNoRedundancy::DataSyncFailed));

    // Verify failovers are not allowed
    EXPECT_FALSE(redInterface.failovers_allowed());
    EXPECT_EQ(redInterface.failovers_not_allowed_reason(),
              Redundancy::FailoversNotAllowedReason::NoRedundancy);

    verifyPersistentData(Role::Active, "Sibling is already passive", false);
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
    auto& sibling = mockProviders->getMockSibling();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    // Configure getPeerConnected to always return false
    ON_CALL(services, getPeerConnected()).WillByDefault(Return(false));

    // Expect active role handler operations
    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(1);
    EXPECT_CALL(services,
                startUnit("obmc-bmc-active.target", std::chrono::seconds(600)))
        .Times(1);

    // Expect sibling wait operations (since sibling is alive)
    EXPECT_CALL(sibling, waitForSiblingRole()).Times(1);
    EXPECT_CALL(sibling, waitForBMCSteadyState()).Times(1);

    // Still runs normally.  Would hit time out in real code.
    EXPECT_CALL(services, waitForPeerConnection()).Times(1);

    // Full sync should not be called.
    EXPECT_CALL(syncInterface, doFullSync()).Times(0);

    EXPECT_CALL(syncInterface, disableBackgroundSync()).Times(1);

    // Expect error log to be created when redundancy can't be enabled
    EXPECT_CALL(services, logError(errors::error_msg::noRedundancy,
                                   errors::Level::Error, _))
        .Times(1);

    createManagerAndRun(Event::activeHandlerStartComplete);

    ASSERT_NE(manager, nullptr);
    const auto& redInterface = manager->getRedundancyInterface();

    // Verify this BMC is Active
    EXPECT_EQ(redInterface.role(), Role::Active);

    // No redundancy
    EXPECT_FALSE(redInterface.redundancy_enabled());

    // Verify the reason for no redundancy includes NetworkError
    const auto& reasons = redInterface.reasons_for_no_redundancy();
    EXPECT_FALSE(reasons.empty());
    EXPECT_TRUE(std::ranges::contains(
        reasons, Redundancy::ReasonForNoRedundancy::NetworkError));

    // Verify failovers are not allowed due to no redundancy
    EXPECT_FALSE(redInterface.failovers_allowed());
    EXPECT_EQ(redInterface.failovers_not_allowed_reason(),
              Redundancy::FailoversNotAllowedReason::NoRedundancy);

    verifyPersistentData(Role::Active, "Sibling is already passive", false);
}

/**
 * @brief Test: Passive BMC successfully fails over and becomes Active
 */
TEST_F(ManagerTest, StartFailover_SuccessfulFailoverToActive)
{
    using namespace std::chrono_literals;

    // Configure scenario: This BMC starts as Passive
    TestScenarioConfig config{.bmcPosition = 1, .siblingRole = Role::Active};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();
    auto& siblingReset = mockProviders->getMockSiblingReset();
    auto& syncInterface = mockProviders->getMockSyncInterface();

    // Configure sibling redundancy properties for failover checks
    // Sibling (Active BMC) has redundancy enabled and failovers allowed
    ON_CALL(sibling, getRedundancyEnabled())
        .WillByDefault(Return(std::optional<bool>(true)));
    ON_CALL(sibling, getFailoversAllowed())
        .WillByDefault(Return(std::optional<bool>(true)));

    // Configure sibling role: Active during startup and failover checks,
    // then Passive after reset. Need multiple Active returns for all the
    // checks.
    EXPECT_CALL(sibling, getRole())
        .Times(AtLeast(1))
        .WillOnce(Return(Role::Active))         // Startup check 1
        .WillOnce(Return(Role::Active))         // Startup check 2
        .WillOnce(Return(Role::Active))         // Failover check 1
        .WillOnce(Return(Role::Active))         // Failover check 2
        .WillOnce(Return(Role::Active))         // Failover check 3
        .WillRepeatedly(Return(Role::Passive)); // After reset

    // Expect passive role handler to start at startup,
    // then active after failover
    EXPECT_CALL(services,
                startUnit("obmc-bmc-passive.target", std::chrono::seconds(300)))
        .Times(1);
    EXPECT_CALL(services,
                startUnit("obmc-bmc-active.target", std::chrono::seconds(600)))
        .Times(1);

    // Expectations for doFailoverFromPassive sequence:

    // 1. Disable background sync (called during passive startup and failover)
    EXPECT_CALL(syncInterface, disableBackgroundSync()).Times(AtLeast(1));

    // 2. Failover imminent delay
    EXPECT_CALL(services, doFailoverImminentDelay()).Times(1);

    // 3. Reset the sibling BMC
    EXPECT_CALL(siblingReset, toggleReset()).Times(1);

    // 4. Active role handler operations after becoming active
    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(1);

    // 5. Expect error log for failover started
    EXPECT_CALL(services, logError(errors::error_msg::failoverStarted,
                                   errors::Level::Informational, _))
        .Times(1);

    manager =
        std::make_unique<Manager>(ctx, std::move(mockProviders), hbInterval);

    spawnFunc(
        [this]() -> sdbusplus::async::task<> {
            using namespace std::chrono_literals;
            // Wait for startup to complete
            co_await waitForEvent(Event::passiveHandlerStartComplete, 1s,
                                  false);

            // Call StartFailover D-Bus method
            FailoverOptions options;
            co_await manager->method_call(Manager::start_failover_t{},
                                          Requester::Host, options);

            co_await waitForEvent(Event::failoverComplete);
        },
        ctx);

    ctx.run();

    ASSERT_NE(manager, nullptr);
    const auto& redInterface = manager->getRedundancyInterface();

    // Verify this BMC is now Active after failover
    EXPECT_EQ(redInterface.role(), Role::Active);

    // Verify failover_in_progress is cleared
    EXPECT_FALSE(redInterface.failover_in_progress());

    // Verify failover_imminent is cleared
    EXPECT_FALSE(redInterface.failover_imminent());

    // Verify redundancy is enabled
    EXPECT_TRUE(redInterface.redundancy_enabled());
    EXPECT_TRUE(redInterface.reasons_for_no_redundancy().empty());

    // Verify failovers are allowed
    EXPECT_TRUE(redInterface.failovers_allowed());
    EXPECT_EQ(redInterface.failovers_not_allowed_reason(),
              Redundancy::FailoversNotAllowedReason::None);

    verifyPersistentData(Role::Active, "Failover", false);
}

/**
 * @brief Test: Passive BMC with redundancy enabled tries to
 *        failover but is blocked because failovers are not allowed.
 */
TEST_F(ManagerTest, FailoverBlocked_NotAllowed)
{
    using namespace std::chrono_literals;

    // Configure scenario: This BMC starts as Passive
    TestScenarioConfig config{.bmcPosition = 1, .siblingRole = Role::Active};
    setupTestScenario(config);

    auto& services = mockProviders->getMockServices();
    auto& sibling = mockProviders->getMockSibling();

    // Configure sibling redundancy properties for failover checks
    // Sibling (Active BMC) has redundancy enabled but failovers not allowed
    ON_CALL(sibling, getRedundancyEnabled())
        .WillByDefault(Return(std::optional<bool>(true)));
    ON_CALL(sibling, getFailoversAllowed())
        .WillByDefault(Return(std::optional<bool>(false)));

    // Expect error log for blocked failover
    EXPECT_CALL(services, logError(errors::error_msg::failoverBlocked,
                                   errors::Level::Warning, _))
        .Times(1);

    // Failover should NOT proceed, so these should not be called
    EXPECT_CALL(services, doFailoverImminentDelay()).Times(0);
    EXPECT_CALL(services, acquireFullHardwareAccess()).Times(0);

    manager =
        std::make_unique<Manager>(ctx, std::move(mockProviders), hbInterval);

    spawnFunc(
        [this]() -> sdbusplus::async::task<> {
            using namespace std::chrono_literals;
            // Wait for startup to complete
            co_await waitForEvent(Event::passiveHandlerStartComplete, 1s,
                                  false);

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

    ctx.run();

    ASSERT_NE(manager, nullptr);
    const auto& redInterface = manager->getRedundancyInterface();

    // Verify this BMC is still Passive (failover was blocked)
    EXPECT_EQ(redInterface.role(), Role::Passive);

    // Verify redundancy is still enabled
    EXPECT_TRUE(redInterface.redundancy_enabled());
    EXPECT_TRUE(redInterface.reasons_for_no_redundancy().empty());

    // Verify no failover in progress
    EXPECT_FALSE(redInterface.failover_in_progress());
    EXPECT_FALSE(redInterface.failover_imminent());

    verifyPersistentData(Role::Passive, "Sibling is already active", false);
}
