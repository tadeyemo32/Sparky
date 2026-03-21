#include <gtest/gtest.h>

// Mock for weapon quality and IDs
struct MockWeapon {
    int quality = 0;
    int itemIDLow = 0;
    int itemIDHigh = 0;
    bool initialized = false;
};

// Logic from SkinChanger.cpp
void RunSkinChanger_TestedLogic(MockWeapon& weapon, int warpaintID, int skinID) {
    if (warpaintID > 0 || skinID > 0) {
        weapon.quality = 15; // Decorated
        weapon.itemIDLow = -1;
        weapon.itemIDHigh = -1;
        weapon.initialized = true;
    }
}

TEST(SkinChangerTest, ApplyWarPaint) {
    MockWeapon weapon;
    RunSkinChanger_TestedLogic(weapon, 100, 0);
    
    EXPECT_EQ(weapon.quality, 15);
    EXPECT_EQ(weapon.itemIDLow, -1);
    EXPECT_EQ(weapon.itemIDHigh, -1);
    EXPECT_TRUE(weapon.initialized);
}

TEST(SkinChangerTest, NoActionWhenDisabled) {
    MockWeapon weapon;
    weapon.quality = 6; // Unique
    weapon.itemIDLow = 1234;
    RunSkinChanger_TestedLogic(weapon, 0, 0);
    
    EXPECT_EQ(weapon.quality, 6);
    EXPECT_EQ(weapon.itemIDLow, 1234);
    EXPECT_FALSE(weapon.initialized);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
