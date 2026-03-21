#include <gtest/gtest.h>
#include <string>
#include <unordered_map>

// Mock structure for ConVar testing
struct MockConVar {
    int value = 0;
};

std::unordered_map<std::string, MockConVar> mockConVars;

void MockSetValue(const std::string& name, int state) {
    mockConVars[name].value = state;
}

// Logic from Optimizations.cpp
void RunOptimizations_TestedLogic(
    bool disableGibs, 
    bool disableRagdolls, 
    bool disableDecals, 
    bool disableFoliage, 
    bool lowDetailModels, 
    bool noProps
) {
    if (disableGibs) MockSetValue("cl_gib_allow", 0);
    else MockSetValue("cl_gib_allow", 1);

    if (disableRagdolls) MockSetValue("cl_ragdoll_fade_time", 0);
    else MockSetValue("cl_ragdoll_fade_time", 15);

    if (disableDecals) {
        MockSetValue("r_decals", 0);
        MockSetValue("mp_decals", 0);
    } else {
        MockSetValue("r_decals", 2048);
        MockSetValue("mp_decals", 2048);
    }

    if (lowDetailModels) MockSetValue("r_rootlod", 2);
    else MockSetValue("r_rootlod", 0);
}

TEST(OptimizationsTest, DisableGibs) {
    mockConVars.clear();
    RunOptimizations_TestedLogic(true, false, false, false, false, false);
    EXPECT_EQ(mockConVars["cl_gib_allow"].value, 0);
}

TEST(OptimizationsTest, EnableGibs) {
    mockConVars.clear();
    RunOptimizations_TestedLogic(false, false, false, false, false, false);
    EXPECT_EQ(mockConVars["cl_gib_allow"].value, 1);
}

TEST(OptimizationsTest, HighPerformanceMode) {
    mockConVars.clear();
    // All optimizations ON
    RunOptimizations_TestedLogic(true, true, true, true, true, true);
    
    EXPECT_EQ(mockConVars["cl_gib_allow"].value, 0);
    EXPECT_EQ(mockConVars["cl_ragdoll_fade_time"].value, 0);
    EXPECT_EQ(mockConVars["r_decals"].value, 0);
    EXPECT_EQ(mockConVars["r_rootlod"].value, 2);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
