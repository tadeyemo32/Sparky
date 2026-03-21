// test_math.cpp - Math utilities tests (Google Test)
#include <gtest/gtest.h>
#include "test_types.h"
#include "Utils/Math/Math.h"

TEST(MathTest, RemapVal) { EXPECT_FLOAT_EQ(Math::RemapVal(50, 0, 100, 0, 10), 5.f); }
TEST(MathTest, RemapValClampUpper) { EXPECT_FLOAT_EQ(Math::RemapVal(150, 0, 100, 0, 10), 10.f); }
TEST(MathTest, RemapValClampLower) { EXPECT_FLOAT_EQ(Math::RemapVal(-50, 0, 100, 0, 10), 0.f); }
TEST(MathTest, RemapValUnclamped) { EXPECT_FLOAT_EQ(Math::RemapVal(150, 0, 100, 0, 10, false), 15.f); }

TEST(MathTest, SimpleSpline) { EXPECT_FLOAT_EQ(Math::SimpleSpline(0.5f), 0.5f); }
TEST(MathTest, SimpleSplineRemapVal) { EXPECT_FLOAT_EQ(Math::SimpleSplineRemapVal(50, 0, 100, 0, 10), 5.f); }

TEST(MathTest, Lerp) { EXPECT_FLOAT_EQ(Math::Lerp(10.f, 20.f, 0.5f), 15.f); }
TEST(MathTest, NormalizeAngleInPlace) { float a = 400.f; a = Math::NormalizeAngle(a); EXPECT_FLOAT_EQ(a, 40.f); }

TEST(MathTest, VectorAngles) {
    Vec3 r = Math::VectorAngles({1, 0, 0});
    EXPECT_NEAR(Math::NormalizeAngle(r.x), 0.f, 1e-3); EXPECT_NEAR(Math::NormalizeAngle(r.y), 0.f, 1e-3);
    Vec3 r2 = Math::VectorAngles({0, 1, 0});
    EXPECT_NEAR(Math::NormalizeAngle(r2.x), 0.f, 1e-3); EXPECT_NEAR(Math::NormalizeAngle(r2.y), 90.f, 1e-3);
    Vec3 r3 = Math::VectorAngles({0, 0, 1});
    EXPECT_NEAR(Math::NormalizeAngle(r3.x), -90.f, 1e-3); EXPECT_NEAR(Math::NormalizeAngle(r3.y), 0.f, 1e-3);
}

TEST(MathTest, AngleVectors) {
    Vec3 f; Math::AngleVectors({0, 0, 0}, &f);
    EXPECT_NEAR(f.x, 1.f, 1e-3); EXPECT_NEAR(f.y, 0.f, 1e-3); EXPECT_NEAR(f.z, 0.f, 1e-3);
}

TEST(MathTest, CalcAngle) {
    Vec3 ang = Math::CalcAngle({0,0,0}, {100,100,0});
    EXPECT_NEAR(ang.x, 0.f, 1e-3); EXPECT_NEAR(ang.y, 45.f, 1e-3);
}

TEST(MathTest, CalcFov) {
    EXPECT_NEAR(Math::CalcFov({0,0,0}, {0,45,0}), 45.f, 1e-3);
}

TEST(MathTest, RayToOBB) {
    Vec3 start(-50,0,0), end(50,0,0), mins(-10,-10,-10), maxs(10,10,10);
    VMatrix mat; mat.SetupMatrixOrgAngles({0,0,0}, {0,0,0});
    EXPECT_TRUE(Math::RayToOBB(start, (end-start).Normalized(), mins, maxs, mat.As3x4()));
}

TEST(MathTest, AngleMatrix) {
    Vec3 start(0,0,0);
    matrix3x4 mat; Math::AngleMatrix({0, 90, 0}, mat);
    // basic spot check
}

TEST(MathTest, SolveQuadratic) {
    auto res = Math::SolveQuadratic(1, -3, 2);
    ASSERT_EQ(res.size(), 2);
    EXPECT_FLOAT_EQ(std::min(res[0], res[1]), 1.f);
    EXPECT_FLOAT_EQ(std::max(res[0], res[1]), 2.f);
}

TEST(MathTest, SolveCubic) {
    float res = Math::SolveCubic(-6, 11, -6);
    EXPECT_FLOAT_EQ(res, 2.f); // One of the roots (1, 2, 3) is 2.f
}
