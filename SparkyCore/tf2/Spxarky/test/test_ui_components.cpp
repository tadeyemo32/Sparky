// test_ui_components.cpp - UI components integration tests (Google Test)
#include <gtest/gtest.h>
#include "test_types.h"

// ---------------------------------------------------------
// Dummy ImGui state implementations for testing
namespace ImGui {
    bool IsColorBright(Color_t clr);
    std::string StripDoubleHash(std::string_view name);
    bool IsMouseWithin(Vec2 Mouse, Vec2 Pos, Vec2 Size);

    static int s_ClickedButtonCount = 0;
    static std::string s_LastButtonName = "";

    bool Button(const char* label, const Vec2& size) {
        // Simulate a button press for the test's sake
        s_LastButtonName = StripDoubleHash(label);
        if (s_LastButtonName == "TestButton") {
            s_ClickedButtonCount++;
            return true;
        }
        return false;
    }
}

TEST(UIComponentTest, ButtonClickSimulation) {
    ImGui::s_ClickedButtonCount = 0;
    bool result = ImGui::Button("TestButton##123", {100, 30});
    EXPECT_TRUE(result);
    EXPECT_EQ(ImGui::s_ClickedButtonCount, 1);
    EXPECT_EQ(ImGui::s_LastButtonName, "TestButton");
}

TEST(UIComponentTest, ComponentColorLogic) {
    Color_t bg_dark(20, 20, 20, 255);
    Color_t text_bright(240, 240, 240, 255);
    
    EXPECT_FALSE(ImGui::IsColorBright(bg_dark));
    EXPECT_TRUE(ImGui::IsColorBright(text_bright));
    
    // Simulate contrast checks for UI component styling
    bool good_contrast = (ImGui::IsColorBright(text_bright) != ImGui::IsColorBright(bg_dark));
    EXPECT_TRUE(good_contrast);
}

TEST(UIComponentTest, HitboxBoundsCheck) {
    // Simulate UI component containing mouse
    Vec2 component_pos(300, 200);
    Vec2 component_size(150, 50);
    
    // Mouse directly inside
    Vec2 mouse_inside(350, 225);
    EXPECT_TRUE(ImGui::IsMouseWithin(mouse_inside, component_pos, component_size));
    
    // Mouse just outside bounds
    Vec2 mouse_left(299, 225);
    Vec2 mouse_top(350, 199);
    Vec2 mouse_right(451, 225);
    Vec2 mouse_bottom(350, 251);
    
    EXPECT_FALSE(ImGui::IsMouseWithin(mouse_left, component_pos, component_size));
    EXPECT_FALSE(ImGui::IsMouseWithin(mouse_top, component_pos, component_size));
    EXPECT_FALSE(ImGui::IsMouseWithin(mouse_right, component_pos, component_size));
    EXPECT_FALSE(ImGui::IsMouseWithin(mouse_bottom, component_pos, component_size));
}
