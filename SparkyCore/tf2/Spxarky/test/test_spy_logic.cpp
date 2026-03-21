#include <gtest/gtest.h>
#include "test_types.h"

// Replicated logic from Spy.cpp for testing
const int MOCK_IN_ATTACK2 = (1 << 11);

void Spy_Run_TestedLogic(int tickcount, int& buttons, bool isCloaked, bool fastCloak, bool fastUncloak)
{
    // Fast Cloak
    if (fastCloak && !isCloaked && (buttons & MOCK_IN_ATTACK2))
    {
        if (tickcount % 2 == 0)
            buttons &= ~MOCK_IN_ATTACK2;
    }

    // Fast Decloak (Uncloak)
    if (fastUncloak && isCloaked && (buttons & MOCK_IN_ATTACK2))
    {
        if (tickcount % 2 == 0)
            buttons &= ~MOCK_IN_ATTACK2;
    }
}

TEST(SpyTest, FastCloakPulsing) {
    int buttons = MOCK_IN_ATTACK2;
    
    // Tick 0 (Even) -> Should be cleared
    Spy_Run_TestedLogic(0, buttons, false, true, false);
    EXPECT_FALSE(buttons & MOCK_IN_ATTACK2);
    
    // Tick 1 (Odd) -> Should remain
    buttons = MOCK_IN_ATTACK2;
    Spy_Run_TestedLogic(1, buttons, false, true, false);
    EXPECT_TRUE(buttons & MOCK_IN_ATTACK2);
}

TEST(SpyTest, FastUncloakPulsing) {
    int buttons = MOCK_IN_ATTACK2;
    
    // Tick 2 (Even) -> Should be cleared
    Spy_Run_TestedLogic(2, buttons, true, false, true);
    EXPECT_FALSE(buttons & MOCK_IN_ATTACK2);
    
    // Tick 3 (Odd) -> Should remain
    buttons = MOCK_IN_ATTACK2;
    Spy_Run_TestedLogic(3, buttons, true, false, true);
    EXPECT_TRUE(buttons & MOCK_IN_ATTACK2);
}

TEST(SpyTest, NoPulsingWhenDisabled) {
    int buttons = MOCK_IN_ATTACK2;
    
    Spy_Run_TestedLogic(0, buttons, false, false, false);
    EXPECT_TRUE(buttons & MOCK_IN_ATTACK2);
    
    Spy_Run_TestedLogic(2, buttons, true, false, false);
    EXPECT_TRUE(buttons & MOCK_IN_ATTACK2);
}
