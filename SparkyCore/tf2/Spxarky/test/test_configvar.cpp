// test_configvar.cpp - ConfigVar system tests (Google Test)
#include <gtest/gtest.h>
#include "test_types.h"
#include "Utils/ConVars/ConVars.h"

TEST(ConfigVarTest, BoolVar) {
    ConfigVar<bool> bMap("TestBool", false);
    EXPECT_FALSE(bMap.Value);
    bMap.Value = true;
    EXPECT_TRUE(bMap.Value);
}

TEST(ConfigVarTest, IntVar) {
    ConfigVar<int> iMap("TestInt", 42);
    EXPECT_EQ(iMap.Value, 42);
    iMap.Value = 100;
    EXPECT_EQ(iMap.Value, 100);
}

TEST(ConfigVarTest, FloatVar) {
    ConfigVar<float> fMap("TestFloat", 3.14f);
    EXPECT_FLOAT_EQ(fMap.Value, 3.14f);
}

TEST(ConfigVarTest, StringMap) {
    ConfigVar<std::map<std::string, bool>> sMap("TestMap", {{"A", true}, {"B", false}});
    EXPECT_TRUE(sMap.Value["A"]);
    EXPECT_FALSE(sMap.Value["B"]);
    sMap.Value["C"] = true;
    EXPECT_TRUE(sMap.Value["C"]);
}

TEST(ConfigVarTest, RangeConstructor) {
    ConfigVar<int> rangeVar("TestRange", 5, 0, 10);
    EXPECT_EQ(rangeVar.Value, 5);
}
