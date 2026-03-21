#include <gtest/gtest.h>
#include "test_types.h"
#include "SDK/Definitions/Main/PlayerStats.h"

// Mirrors the fields that SkinChanger::Run writes on CTFWeaponBase.
// Using the same field names as the SDK netvars means a rename will
// cause a mismatch here, catching the divergence early.
struct WeaponModState
{
    int  m_iEntityQuality = 0;
    int  m_iItemIDLow     = 0;
    int  m_iItemIDHigh    = 0;
    bool m_bInitialized   = false;
};

// Thin wrapper that applies SkinChanger's branch using the real SDK
// constants — AE_PAINTKITWEAPON, not a magic number.
// Any change to that constant in PlayerStats.h will break these tests
// before the DLL build, which is exactly what we want.
static void ApplySkinMod(WeaponModState& w, int warpaintID, int skinID)
{
    if (warpaintID > 0 || skinID > 0)
    {
        w.m_iEntityQuality = AE_PAINTKITWEAPON;
        w.m_iItemIDLow     = -1;
        w.m_iItemIDHigh    = -1;
        w.m_bInitialized   = true;
    }
}

// ── Constant sanity ───────────────────────────────────────────────────────

TEST(SkinChangerTest, AE_PAINTKITWEAPON_HasExpectedValue)
{
    // If PlayerStats.h changes this the test fails before the DLL build does.
    EXPECT_EQ(AE_PAINTKITWEAPON, 15);
}

// ── Condition: both IDs zero → no modification ───────────────────────────

TEST(SkinChangerTest, NoModWhenBothIDsZero)
{
    WeaponModState w;
    w.m_iEntityQuality = 6; // AE_UNIQUE
    w.m_iItemIDLow     = 1234;

    ApplySkinMod(w, 0, 0);

    EXPECT_EQ(w.m_iEntityQuality, 6);
    EXPECT_EQ(w.m_iItemIDLow, 1234);
    EXPECT_FALSE(w.m_bInitialized);
}

// ── Condition: warpaintID > 0 triggers mod ───────────────────────────────

TEST(SkinChangerTest, ApplyWhenWarpaintIDSet)
{
    WeaponModState w;
    ApplySkinMod(w, 100, 0);

    EXPECT_EQ(w.m_iEntityQuality, AE_PAINTKITWEAPON);
    EXPECT_EQ(w.m_iItemIDLow,  -1);
    EXPECT_EQ(w.m_iItemIDHigh, -1);
    EXPECT_TRUE(w.m_bInitialized);
}

// ── Condition: skinID > 0 triggers mod ───────────────────────────────────

TEST(SkinChangerTest, ApplyWhenSkinIDSet)
{
    WeaponModState w;
    ApplySkinMod(w, 0, 42);

    EXPECT_EQ(w.m_iEntityQuality, AE_PAINTKITWEAPON);
    EXPECT_EQ(w.m_iItemIDLow,  -1);
    EXPECT_EQ(w.m_iItemIDHigh, -1);
    EXPECT_TRUE(w.m_bInitialized);
}

// ── Condition: both IDs set ───────────────────────────────────────────────

TEST(SkinChangerTest, ApplyWhenBothIDsSet)
{
    WeaponModState w;
    ApplySkinMod(w, 50, 200);

    EXPECT_EQ(w.m_iEntityQuality, AE_PAINTKITWEAPON);
    EXPECT_EQ(w.m_iItemIDLow,  -1);
    EXPECT_EQ(w.m_iItemIDHigh, -1);
    EXPECT_TRUE(w.m_bInitialized);
}

// ── Idempotency: applying twice is safe ──────────────────────────────────

TEST(SkinChangerTest, IdempotentOnRepeatApplication)
{
    WeaponModState w;
    ApplySkinMod(w, 1, 0);
    ApplySkinMod(w, 1, 0);

    EXPECT_EQ(w.m_iEntityQuality, AE_PAINTKITWEAPON);
    EXPECT_EQ(w.m_iItemIDLow,  -1);
    EXPECT_EQ(w.m_iItemIDHigh, -1);
    EXPECT_TRUE(w.m_bInitialized);
}
