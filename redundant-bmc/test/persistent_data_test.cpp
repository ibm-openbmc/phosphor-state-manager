#include "persistent_data.hpp"
#include "role_determination.hpp"

#include <fstream>

#include <gtest/gtest.h>

using namespace rbmc;
using namespace role_determination;

class PersistentDataTest : public ::testing::Test
{
  protected:
    static void SetUpTestCase()
    {
        char d[] = "/tmp/datatestXXXXXX";
        saveFile = mkdtemp(d);
        saveFile /= "save.json";
    }

    static void TearDownTestCase()
    {
        std::filesystem::remove_all(saveFile.parent_path());
    }

    static std::filesystem::path saveFile;
};

std::filesystem::path PersistentDataTest::saveFile;

TEST_F(PersistentDataTest, WriteAndReadTest)
{
    // Write
    data::write("Role", Role::Active, saveFile);
    data::write("Bool", true, saveFile);
    data::write("String", std::string{"String"}, saveFile);
    data::write("Uint32", uint32_t{0xAABBCCDD}, saveFile);

    // Read back
    EXPECT_EQ(data::read<Role>("Role", saveFile), Role::Active);
    EXPECT_EQ(data::read<bool>("Bool", saveFile), true);
    EXPECT_EQ(data::read<std::string>("String", saveFile),
              std::string{"String"});
    EXPECT_EQ(data::read<uint32_t>("Uint32", saveFile), 0xAABBCCDD);

    // Write new values
    data::write("Role", Role::Passive, saveFile);
    data::write("Bool", false, saveFile);
    data::write("String", std::string{"New"}, saveFile);
    data::write("Uint32", uint32_t{0x12345678}, saveFile);

    // Read back the new values
    EXPECT_EQ(data::read<Role>("Role", saveFile), Role::Passive);
    EXPECT_EQ(data::read<bool>("Bool", saveFile), false);
    EXPECT_EQ(data::read<std::string>("String", saveFile), std::string{"New"});
    EXPECT_EQ(data::read<uint32_t>("Uint32", saveFile), 0x12345678);

    // Some different types - write
    data::write("EmptyString", std::string{}, saveFile);
    data::write("VectorOfStrings", std::vector<std::string>{"a", "b"},
                saveFile);
    data::write("EmptyVector", std::vector<std::string>{}, saveFile);
    data::write("Map", std::map<int, std::string>{{1, "one"}, {2, "two"}},
                saveFile);
    data::write("EmptyMap", std::map<int, std::string>{}, saveFile);

    // Some different types - read back
    EXPECT_EQ(data::read<std::string>("EmptyString", saveFile), std::string{});
    EXPECT_EQ(data::read<std::vector<std::string>>("VectorOfStrings", saveFile),
              (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(data::read<std::vector<std::string>>("EmptyVector", saveFile),
              (std::vector<std::string>{}));

    EXPECT_EQ((data::read<std::map<int, std::string>>("Map", saveFile)),
              (std::map<int, std::string>{{1, "one"}, {2, "two"}}));
    EXPECT_EQ((data::read<std::map<int, std::string>>("EmptyMap", saveFile)),
              (std::map<int, std::string>{}));

    // Key doesn't exist
    EXPECT_EQ(data::read<Role>("Blah", saveFile), std::nullopt);

    // File doesn't exist
    EXPECT_EQ(data::read<Role>("Role", "/blah/blah"), std::nullopt);

    // Invalid JSON
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

    EXPECT_EQ(data::read<Role>("Role", saveFile), std::nullopt);
}

TEST_F(PersistentDataTest, RemoveTest)
{
    // Write three
    data::write("Role", Role::Active, saveFile);
    data::write("Bool", true, saveFile);
    data::write("String", std::string{"String"}, saveFile);

    // Remove the last one
    data::remove("String", saveFile);
    EXPECT_EQ(data::read<std::string>("String", saveFile), std::nullopt);

    // Make sure other ones still there
    EXPECT_EQ(data::read<Role>("Role", saveFile), Role::Active);
    EXPECT_EQ(data::read<bool>("Bool", saveFile), true);

    // now remove remaining ones
    data::remove("Role", saveFile);
    EXPECT_EQ(data::read<Role>("Role", saveFile), std::nullopt);

    data::remove("Bool", saveFile);
    EXPECT_EQ(data::read<bool>("Bool", saveFile), std::nullopt);

    // Not found
    data::remove("Blah");
}
