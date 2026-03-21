#include <gtest/gtest.h>
#include "test_types.h"
#include "../Spxarky/src/Utils/Math/Math.h"

// Mock for Vars to avoid full SDK dependency in test
struct {
    struct {
        struct {
            bool Facestab = false;
        } Melee;
    } Aimbot;
} MockVars;

// The logic we're testing (replicated from AimbotMelee.cpp)
bool CanBackstab_TestedLogic(const Vec3& localPos, const Vec3& localAngles, const Vec3& targetPos, const Vec3& targetAngles, bool facestabEnabled)
{
    if (facestabEnabled)
        return true;

    Vec3 vForward; Math::AngleVectors(localAngles, &vForward);
    Vec3 vTargetForward; Math::AngleVectors(targetAngles, &vTargetForward);

    Vec3 vToTarget = targetPos - localPos;
    vToTarget.z = 0;
    vToTarget.Normalize();

    float flDot = vToTarget.Dot(vTargetForward);
    if (flDot > 0.5f)
    {
        float flOwnerDot = vForward.Dot(vTargetForward);
        if (flOwnerDot > 0.5f)
            return true;
    }

    return false;
}

TEST(AimbotMeleeTest, StandardBackstab) {
    Vec3 localPos(0, 0, 0);
    Vec3 localAngles(0, 0, 0); // Looking North (+X)
    Vec3 targetPos(100, 0, 0); // Target is in front of us
    Vec3 targetAngles(0, 0, 0); // Target is also looking North

    // Behind target, same direction -> TRUE
    EXPECT_TRUE(CanBackstab_TestedLogic(localPos, localAngles, targetPos, targetAngles, false));
}

TEST(AimbotMeleeTest, FacingTarget_NoFacestab) {
    Vec3 localPos(0, 0, 0);
    Vec3 localAngles(0, 0, 0); // Looking North (+X)
    Vec3 targetPos(100, 0, 0); // Target is in front
    Vec3 targetAngles(0, 180, 0); // Target looking South (-X)

    // Facing target -> FALSE without facestab
    EXPECT_FALSE(CanBackstab_TestedLogic(localPos, localAngles, targetPos, targetAngles, false));
}

TEST(AimbotMeleeTest, FacingTarget_WithFacestab) {
    Vec3 localPos(0, 0, 0);
    Vec3 localAngles(0, 0, 0);
    Vec3 targetPos(100, 0, 0);
    Vec3 targetAngles(0, 180, 0);

    // Facing target -> TRUE with facestab
    EXPECT_TRUE(CanBackstab_TestedLogic(localPos, localAngles, targetPos, targetAngles, true));
}

TEST(AimbotMeleeTest, SideStab_Strict) {
    Vec3 localPos(0, 0, 0);
    Vec3 localAngles(0, 0, 0); // North
    Vec3 targetPos(100, 0, 0);
    Vec3 targetAngles(0, 90, 0); // West

    // Sideways -> FALSE (dot product < 0.5)
    EXPECT_FALSE(CanBackstab_TestedLogic(localPos, localAngles, targetPos, targetAngles, false));
}
