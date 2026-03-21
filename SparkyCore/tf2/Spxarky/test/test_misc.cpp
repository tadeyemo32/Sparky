// test_misc.cpp - Misc utilities tests (Google Test)
#include <gtest/gtest.h>
#include "test_types.h"

// Basic misc string and memory util tests
TEST(MiscTest, FloatToStringPrecision) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", 3.14159f);
    EXPECT_STREQ(buf, "3.14");
    
    snprintf(buf, sizeof(buf), "%.4f", 123.45678f);
    EXPECT_STREQ(buf, "123.4568");
}

TEST(MiscTest, ColorManipulation) {
    Color_t c1(255, 0, 0, 255);
    Color_t c2(0, 0, 255, 255);
    
    // Simulate blending 50%
    Color_t blended(
        (c1.r + c2.r) / 2,
        (c1.g + c2.g) / 2,
        (c1.b + c2.b) / 2,
        255
    );
    
    EXPECT_EQ(blended.r, 127);
    EXPECT_EQ(blended.g, 0);
    EXPECT_EQ(blended.b, 127);
    EXPECT_EQ(blended.a, 255);
}

TEST(MiscTest, TimeTicksMath) {
    float latency = 0.045f;
    float tick_interval = 0.015f;
    
    int ticks = static_cast<int>(latency / tick_interval);
    EXPECT_EQ(ticks, 3);
    
    // Test TIME_TO_TICKS macro equivalent (adds 0.5f)
    int rounded_ticks = static_cast<int>(0.5f + (0.040f / tick_interval));
    EXPECT_EQ(rounded_ticks, 3);
}

// Ensure the memory layout of Color_t matches standard constraints
TEST(MiscTest, ColorSize) {
    EXPECT_EQ(sizeof(Color_t), 4);
}
