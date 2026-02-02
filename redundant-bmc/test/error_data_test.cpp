// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#include "error_data.hpp"
#include "types.hpp"

#include <gtest/gtest.h>

using namespace rbmc::errors;
using rbmc::FailoverOptions;

TEST(ErrorDataTest, AddFailoverOptsToData_EmptyOptions)
{
    // Test with empty FailoverOptions
    FailoverOptions options;
    AdditionalData data;

    addFailoverOptsToData(options, data);

    EXPECT_TRUE(data.empty());
}

TEST(ErrorDataTest, AddFailoverOptsToData_TrueAndFalse)
{
    FailoverOptions options{{"option1", true}, {"option2", false}};
    AdditionalData data;

    addFailoverOptsToData(options, data);

    ASSERT_EQ(data.size(), 2);
    EXPECT_EQ(data["FOOpt:option1"], "true");
    EXPECT_EQ(data["FOOpt:option2"], "false");
}

TEST(ErrorDataTest, AddFailoverOptsToData_PreserveExistingData)
{
    // Test that existing data in AdditionalData is preserved
    AdditionalData data{{"ExistingKey1", "ExistingValue1"},
                        {"ExistingKey2", "ExistingValue2"}};

    FailoverOptions options{{"option1", false},
                            {"xyz.openbmc_project.Option2", true}};

    addFailoverOptsToData(options, data);

    ASSERT_EQ(data.size(), 4);
    EXPECT_EQ(data["ExistingKey1"], "ExistingValue1");
    EXPECT_EQ(data["ExistingKey2"], "ExistingValue2");
    EXPECT_EQ(data["FOOpt:option1"], "false");
    EXPECT_EQ(data["FOOpt:xyz.openbmc_project.Option2"], "true");
}

TEST(ErrorDataTest, AddFailoverOptsToData_EmptyKeyString)
{
    // Test with empty key string (edge case)
    FailoverOptions options{{"", true}};
    AdditionalData data;

    addFailoverOptsToData(options, data);

    ASSERT_EQ(data.size(), 1);
    EXPECT_EQ(data["FOOpt:"], "true");
}
