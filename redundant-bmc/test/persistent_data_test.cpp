#include "persistent_data.hpp"
#include "persistent_data_test_fixture.hpp"
#include "role_determination.hpp"

#include <fstream>

#include <gtest/gtest.h>

using namespace rbmc;
using namespace rbmc::test;
using namespace role_determination;

class PersistentDataTest : public PersistentDataTestFixture
{};

TEST_F(PersistentDataTest, WriteAndReadTest)
{
    // Write
    data::write("Role", Role::Active);
    data::write("Bool", true);
    data::write("String", std::string{"String"});
    data::write("Uint32", uint32_t{0xAABBCCDD});

    // Read back
    EXPECT_EQ(data::read<Role>("Role"), Role::Active);
    EXPECT_EQ(data::read<bool>("Bool"), true);
    EXPECT_EQ(data::read<std::string>("String"), std::string{"String"});
    EXPECT_EQ(data::read<uint32_t>("Uint32"), 0xAABBCCDD);

    // Write new values
    data::write("Role", Role::Passive);
    data::write("Bool", false);
    data::write("String", std::string{"New"});
    data::write("Uint32", uint32_t{0x12345678});

    // Read back the new values
    EXPECT_EQ(data::read<Role>("Role"), Role::Passive);
    EXPECT_EQ(data::read<bool>("Bool"), false);
    EXPECT_EQ(data::read<std::string>("String"), std::string{"New"});
    EXPECT_EQ(data::read<uint32_t>("Uint32"), 0x12345678);

    // Some different types - write
    data::write("EmptyString", std::string{});
    data::write("VectorOfStrings", std::vector<std::string>{"a", "b"});
    data::write("EmptyVector", std::vector<std::string>{});
    data::write("Map", std::map<int, std::string>{{1, "one"}, {2, "two"}});
    data::write("EmptyMap", std::map<int, std::string>{});

    // Some different types - read back
    EXPECT_EQ(data::read<std::string>("EmptyString"), std::string{});
    EXPECT_EQ(data::read<std::vector<std::string>>("VectorOfStrings"),
              (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(data::read<std::vector<std::string>>("EmptyVector"),
              (std::vector<std::string>{}));

    EXPECT_EQ((data::read<std::map<int, std::string>>("Map")),
              (std::map<int, std::string>{{1, "one"}, {2, "two"}}));
    EXPECT_EQ((data::read<std::map<int, std::string>>("EmptyMap")),
              (std::map<int, std::string>{}));

    // Key doesn't exist
    EXPECT_EQ(data::read<Role>("Blah"), std::nullopt);

    // Invalid JSON
    auto saveFile = data::dataFile();
    std::filesystem::remove(saveFile);
    std::ofstream file{saveFile};
    const char* data = R"(
        {
            "Role": 1,
            Bool 0
        }
    )";
    file << data;
    file.close();

    EXPECT_EQ(data::read<Role>("Role"), std::nullopt);
}

TEST_F(PersistentDataTest, RemoveTest)
{
    // Write three
    data::write("Role", Role::Active);
    data::write("Bool", true);
    data::write("String", std::string{"String"});

    // Remove the last one
    data::remove("String");
    EXPECT_EQ(data::read<std::string>("String"), std::nullopt);

    // Make sure other ones still there
    EXPECT_EQ(data::read<Role>("Role"), Role::Active);
    EXPECT_EQ(data::read<bool>("Bool"), true);

    // now remove remaining ones
    data::remove("Role");
    EXPECT_EQ(data::read<Role>("Role"), std::nullopt);

    data::remove("Bool");
    EXPECT_EQ(data::read<bool>("Bool"), std::nullopt);

    // Not found
    data::remove("Blah");
}

TEST_F(PersistentDataTest, FailoverLogTest)
{
    using Requester =
        sdbusplus::common::xyz::openbmc_project::control::Failover::Requester;

    data::logFailover(dataDir, 0, Requester::Host, time(nullptr));

    ASSERT_TRUE(std::filesystem::exists(dataDir / "bmc0_failovers"));

    auto logs = data::getFailoverLogs(dataDir, 0);

    ASSERT_EQ(logs.size(), 1);
    EXPECT_EQ(logs.begin()->first, "Host");
    EXPECT_TRUE(!logs.begin()->second.empty());

    // log 9 more so there are now the max of 10
    for ([[maybe_unused]] auto _ : std::views::iota(0, 9))
    {
        data::logFailover(dataDir, 0, Requester::Tool, time(nullptr));
    }

    logs = data::getFailoverLogs(dataDir, 0);

    ASSERT_EQ(logs.size(), 10);
    EXPECT_EQ(logs.begin()->first, "Host");
    EXPECT_EQ(logs.back().first, "Tool");

    // Add one more to cause oldest to be dropped
    data::logFailover(dataDir, 0, Requester::SystemConfig, time(nullptr));

    logs = data::getFailoverLogs(dataDir, 0);

    ASSERT_EQ(logs.size(), 10);
    EXPECT_EQ(logs.begin()->first, "Tool");
    EXPECT_EQ(logs.back().first, "SystemConfig");
}
