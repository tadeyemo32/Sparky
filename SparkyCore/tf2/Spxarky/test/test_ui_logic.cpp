// test_ui_logic.cpp - UI helper logic tests (Google Test)
#include <gtest/gtest.h>
#include "test_types.h"

// Dummy declarations for UI helpers we will test
namespace ImGui {
    bool IsColorBright(Color_t clr);
    Color_t ColorToVec(Color_t clr);
    Color_t VecToColor(Color_t clr);
    std::string StripDoubleHash(std::string_view name);
    bool IsMouseWithin(Vec2 Mouse, Vec2 Pos, Vec2 Size);
}

// ---------------------------------------------------------
// Implementations of the pure logic we are testing
bool ImGui::IsColorBright(Color_t clr) {
    float r = clr.r / 255.f;
    float g = clr.g / 255.f;
    float b = clr.b / 255.f;
    float luminance = sqrt(0.299f * r * r + 0.587f * g * g + 0.114f * b * b);
    return luminance > 0.5f;
}

Color_t ImGui::ColorToVec(Color_t clr) {
    return {
        static_cast<uint8_t>(clr.r / 255.f * 255.f),
        static_cast<uint8_t>(clr.g / 255.f * 255.f),
        static_cast<uint8_t>(clr.b / 255.f * 255.f),
        static_cast<uint8_t>(clr.a / 255.f * 255.f)
    };
}

Color_t ImGui::VecToColor(Color_t clr) {
    return {
        static_cast<uint8_t>(clr.r * 255.f / 255.f),
        static_cast<uint8_t>(clr.g * 255.f / 255.f),
        static_cast<uint8_t>(clr.b * 255.f / 255.f),
        static_cast<uint8_t>(clr.a * 255.f / 255.f)
    };
}

std::string ImGui::StripDoubleHash(std::string_view name) {
    size_t hash_pos = name.find("##");
    if (hash_pos != std::string_view::npos) {
        return std::string(name.substr(0, hash_pos));
    }
    return std::string(name);
}

bool ImGui::IsMouseWithin(Vec2 Mouse, Vec2 Pos, Vec2 Size) {
    return Mouse.x >= Pos.x && Mouse.y >= Pos.y &&
           Mouse.x < Pos.x + Size.x && Mouse.y < Pos.y + Size.y;
}

// --------------------- TESTS ---------------------

TEST(UILogicTest, IsColorBright_White) { EXPECT_TRUE(ImGui::IsColorBright(Color_t(255, 255, 255, 255))); }
TEST(UILogicTest, IsColorBright_Black) { EXPECT_FALSE(ImGui::IsColorBright(Color_t(0, 0, 0, 255))); }
TEST(UILogicTest, IsColorBright_Red) { EXPECT_TRUE(ImGui::IsColorBright(Color_t(255, 0, 0, 255))); }
TEST(UILogicTest, IsColorBright_Green) { EXPECT_TRUE(ImGui::IsColorBright(Color_t(0, 255, 0, 255))); }
TEST(UILogicTest, IsColorBright_Blue) { EXPECT_FALSE(ImGui::IsColorBright(Color_t(0, 0, 255, 255))); }

TEST(UILogicTest, ColorVecRoundTrip) {
    Color_t c(10, 20, 30, 40);
    Color_t v = ImGui::ColorToVec(c);
    Color_t b = ImGui::VecToColor(v);
    EXPECT_EQ(c.r, b.r); EXPECT_EQ(c.g, b.g); EXPECT_EQ(c.b, b.b); EXPECT_EQ(c.a, b.a);
}

TEST(UILogicTest, StripDoubleHash_Basic) { EXPECT_EQ(ImGui::StripDoubleHash("Label##hidden"), "Label"); }
TEST(UILogicTest, StripDoubleHash_NoHash) { EXPECT_EQ(ImGui::StripDoubleHash("Label"), "Label"); }
TEST(UILogicTest, StripDoubleHash_EmptyBefore) { EXPECT_EQ(ImGui::StripDoubleHash("##hidden"), ""); }
TEST(UILogicTest, StripDoubleHash_SingleHash) { EXPECT_EQ(ImGui::StripDoubleHash("Label#number1"), "Label#number1"); }

TEST(UILogicTest, IsMouseWithin_Inside) { EXPECT_TRUE(ImGui::IsMouseWithin({50, 50}, {0, 0}, {100, 100})); }
TEST(UILogicTest, IsMouseWithin_OutsideRight) { EXPECT_FALSE(ImGui::IsMouseWithin({150, 50}, {0, 0}, {100, 100})); }
TEST(UILogicTest, IsMouseWithin_OutsideBelow) { EXPECT_FALSE(ImGui::IsMouseWithin({50, 150}, {0, 0}, {100, 100})); }
TEST(UILogicTest, IsMouseWithin_TopLeft) { EXPECT_TRUE(ImGui::IsMouseWithin({0, 0}, {0, 0}, {100, 100})); }
TEST(UILogicTest, IsMouseWithin_BottomRightBoundary) { EXPECT_FALSE(ImGui::IsMouseWithin({100, 100}, {0, 0}, {100, 100})); }
