// test_fnv1a.cpp - FNV1A Hash tests (Google Test)
#include <gtest/gtest.h>
#include "test_types.h"
#include "Utils/Hash/FNV1A.h"

TEST(FNV1ATest, CompileVsRuntimeEquivalence) {
    constexpr uint32_t ct = FNV1A::Hash32Const("Spxarky");
    uint32_t rt = FNV1A::Hash32("Spxarky");
    EXPECT_EQ(ct, rt);
}

TEST(FNV1ATest, KnownHashValues) {
    EXPECT_EQ(FNV1A::Hash32Const(""), 2166136261u);
    EXPECT_EQ(FNV1A::Hash32Const("a"), 3826002220u);
    EXPECT_EQ(FNV1A::Hash32Const("test"), 2949673445u);
}

TEST(FNV1ATest, CaseSensitivity) {
    EXPECT_NE(FNV1A::Hash32("hello"), FNV1A::Hash32("Hello"));
}

TEST(FNV1ATest, Deterministic) {
    EXPECT_EQ(FNV1A::Hash32("same_string"), FNV1A::Hash32("same_string"));
}

TEST(FNV1ATest, CollisionChecks) {
    EXPECT_NE(FNV1A::Hash32("models/weapons/w_models/w_rocket.mdl"), FNV1A::Hash32("models/weapons/w_models/w_grenade_grenadelauncher.mdl"));
}
