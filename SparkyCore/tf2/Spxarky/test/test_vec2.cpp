// test_vec2.cpp - Comprehensive Vec2 tests (Google Test)
#include <gtest/gtest.h>
#include "test_types.h"

// ==================== Constructors ====================
TEST(Vec2Test, DefaultConstructor) {
    Vec2 v;
    EXPECT_FLOAT_EQ(v.x, 0.f);
    EXPECT_FLOAT_EQ(v.y, 0.f);
}

TEST(Vec2Test, ParameterizedConstructor) {
    Vec2 v(3.f, 4.f);
    EXPECT_FLOAT_EQ(v.x, 3.f);
    EXPECT_FLOAT_EQ(v.y, 4.f);
}

TEST(Vec2Test, CopyConstructor) {
    Vec2 a(5.f, 6.f);
    Vec2 b(a);
    EXPECT_FLOAT_EQ(b.x, 5.f);
    EXPECT_FLOAT_EQ(b.y, 6.f);
}

TEST(Vec2Test, PointerConstructor) {
    float arr[2] = { 7.f, 8.f };
    Vec2 v(arr);
    EXPECT_FLOAT_EQ(v.x, 7.f);
    EXPECT_FLOAT_EQ(v.y, 8.f);
}

TEST(Vec2Test, ConstPointerConstructor) {
    const float arr[2] = { 9.f, 10.f };
    Vec2 v(arr);
    EXPECT_FLOAT_EQ(v.x, 9.f);
    EXPECT_FLOAT_EQ(v.y, 10.f);
}

// ==================== Assignment & Index ====================
TEST(Vec2Test, Assignment) {
    Vec2 a(1.f, 2.f), b;
    b = a;
    EXPECT_FLOAT_EQ(b.x, 1.f);
    EXPECT_FLOAT_EQ(b.y, 2.f);
}

TEST(Vec2Test, IndexOperator) {
    Vec2 v(11.f, 22.f);
    EXPECT_FLOAT_EQ(v[0], 11.f);
    EXPECT_FLOAT_EQ(v[1], 22.f);
    v[0] = 33.f;
    EXPECT_FLOAT_EQ(v.x, 33.f);
}

// ==================== Comparison ====================
TEST(Vec2Test, Equality) {
    EXPECT_TRUE(Vec2(1, 2) == Vec2(1, 2));
    EXPECT_FALSE(Vec2(1, 2) == Vec2(1, 3));
}

TEST(Vec2Test, Inequality) {
    EXPECT_TRUE(Vec2(1, 2) != Vec2(1, 3));
    EXPECT_FALSE(Vec2(1, 2) != Vec2(1, 2));
}

TEST(Vec2Test, BoolOperator) {
    EXPECT_FALSE(static_cast<bool>(Vec2(0, 0)));
    EXPECT_TRUE(static_cast<bool>(Vec2(1, 0)));
    EXPECT_TRUE(static_cast<bool>(Vec2(0, 1)));
}

// ==================== Arithmetic ====================
TEST(Vec2Test, Add) {
    Vec2 r = Vec2(1, 2) + Vec2(3, 4);
    EXPECT_FLOAT_EQ(r.x, 4.f);
    EXPECT_FLOAT_EQ(r.y, 6.f);
}

TEST(Vec2Test, Sub) {
    Vec2 r = Vec2(5, 7) - Vec2(2, 3);
    EXPECT_FLOAT_EQ(r.x, 3.f);
    EXPECT_FLOAT_EQ(r.y, 4.f);
}

TEST(Vec2Test, Mul) {
    Vec2 r = Vec2(2, 3) * Vec2(4, 5);
    EXPECT_FLOAT_EQ(r.x, 8.f);
    EXPECT_FLOAT_EQ(r.y, 15.f);
}

TEST(Vec2Test, Div) {
    Vec2 r = Vec2(10, 20) / Vec2(2, 5);
    EXPECT_FLOAT_EQ(r.x, 5.f);
    EXPECT_FLOAT_EQ(r.y, 4.f);
}

TEST(Vec2Test, AddScalar) {
    Vec2 r = Vec2(1, 2) + 10.f;
    EXPECT_FLOAT_EQ(r.x, 11.f);
    EXPECT_FLOAT_EQ(r.y, 12.f);
}

TEST(Vec2Test, SubScalar) {
    Vec2 r = Vec2(10, 20) - 5.f;
    EXPECT_FLOAT_EQ(r.x, 5.f);
    EXPECT_FLOAT_EQ(r.y, 15.f);
}

TEST(Vec2Test, MulScalar) {
    Vec2 r = Vec2(3, 4) * 2.f;
    EXPECT_FLOAT_EQ(r.x, 6.f);
    EXPECT_FLOAT_EQ(r.y, 8.f);
}

TEST(Vec2Test, DivScalar) {
    Vec2 r = Vec2(10, 20) / 5.f;
    EXPECT_FLOAT_EQ(r.x, 2.f);
    EXPECT_FLOAT_EQ(r.y, 4.f);
}

TEST(Vec2Test, CompoundAdd) {
    Vec2 v(1, 2); v += Vec2(3, 4);
    EXPECT_FLOAT_EQ(v.x, 4.f); EXPECT_FLOAT_EQ(v.y, 6.f);
}

TEST(Vec2Test, CompoundSub) {
    Vec2 v(5, 7); v -= Vec2(2, 3);
    EXPECT_FLOAT_EQ(v.x, 3.f); EXPECT_FLOAT_EQ(v.y, 4.f);
}

TEST(Vec2Test, CompoundMulVec) {
    Vec2 v(2, 3); v *= Vec2(4, 5);
    EXPECT_FLOAT_EQ(v.x, 8.f); EXPECT_FLOAT_EQ(v.y, 15.f);
}

TEST(Vec2Test, CompoundDivVec) {
    Vec2 v(10, 20); v /= Vec2(2, 5);
    EXPECT_FLOAT_EQ(v.x, 5.f); EXPECT_FLOAT_EQ(v.y, 4.f);
}

TEST(Vec2Test, CompoundAddScalar) {
    Vec2 v(1, 2); v += 10.f;
    EXPECT_FLOAT_EQ(v.x, 11.f); EXPECT_FLOAT_EQ(v.y, 12.f);
}

TEST(Vec2Test, CompoundSubScalar) {
    Vec2 v(10, 20); v -= 5.f;
    EXPECT_FLOAT_EQ(v.x, 5.f); EXPECT_FLOAT_EQ(v.y, 15.f);
}

TEST(Vec2Test, CompoundMulScalar) {
    Vec2 v(3, 4); v *= 2.f;
    EXPECT_FLOAT_EQ(v.x, 6.f); EXPECT_FLOAT_EQ(v.y, 8.f);
}

TEST(Vec2Test, CompoundDivScalar) {
    Vec2 v(10, 20); v /= 5.f;
    EXPECT_FLOAT_EQ(v.x, 2.f); EXPECT_FLOAT_EQ(v.y, 4.f);
}

// ==================== Methods ====================
TEST(Vec2Test, Zero) {
    Vec2 v(5, 6); v.Zero();
    EXPECT_FLOAT_EQ(v.x, 0.f); EXPECT_FLOAT_EQ(v.y, 0.f);
}

TEST(Vec2Test, Set) {
    Vec2 v; v.Set(7.f, 8.f);
    EXPECT_FLOAT_EQ(v.x, 7.f); EXPECT_FLOAT_EQ(v.y, 8.f);
}

TEST(Vec2Test, Length) {
    EXPECT_NEAR(Vec2(3, 4).Length(), 5.f, 1e-5);
    EXPECT_NEAR(Vec2(0, 0).Length(), 0.f, 1e-5);
    EXPECT_NEAR(Vec2(1, 0).Length(), 1.f, 1e-5);
}

TEST(Vec2Test, LengthSqr) {
    EXPECT_NEAR(Vec2(3, 4).LengthSqr(), 25.f, 1e-5);
}

TEST(Vec2Test, DistTo) {
    EXPECT_NEAR(Vec2(0, 0).DistTo(Vec2(3, 4)), 5.f, 1e-5);
}

TEST(Vec2Test, DistToSqr) {
    EXPECT_NEAR(Vec2(0, 0).DistToSqr(Vec2(3, 4)), 25.f, 1e-5);
}

TEST(Vec2Test, Dot) {
    EXPECT_NEAR(Vec2(1, 0).Dot(Vec2(0, 1)), 0.f, 1e-5);
    EXPECT_NEAR(Vec2(2, 3).Dot(Vec2(4, 5)), 23.f, 1e-5);
}

TEST(Vec2Test, DotNormalized) {
    EXPECT_NEAR(Vec2(1, 0).DotNormalized(Vec2(1, 0)), 1.f, 1e-5);
    EXPECT_NEAR(Vec2(1, 0).DotNormalized(Vec2(0, 1)), 0.f, 1e-5);
    EXPECT_NEAR(Vec2(1, 0).DotNormalized(Vec2(-1, 0)), -1.f, 1e-5);
}

TEST(Vec2Test, IsZero) {
    EXPECT_TRUE(Vec2(0, 0).IsZero());
    EXPECT_TRUE(Vec2(0.0005f, 0.0005f).IsZero());
    EXPECT_FALSE(Vec2(1, 0).IsZero());
}

TEST(Vec2Test, MinMaxScalar) {
    EXPECT_FLOAT_EQ(Vec2(3, 1).Min(), 1.f);
    EXPECT_FLOAT_EQ(Vec2(3, 1).Max(), 3.f);
}

TEST(Vec2Test, MinMaxVec) {
    Vec2 mn = Vec2(3, 1).Min(Vec2(2, 5));
    EXPECT_FLOAT_EQ(mn.x, 2.f); EXPECT_FLOAT_EQ(mn.y, 1.f);
    Vec2 mx = Vec2(3, 1).Max(Vec2(2, 5));
    EXPECT_FLOAT_EQ(mx.x, 3.f); EXPECT_FLOAT_EQ(mx.y, 5.f);
}

TEST(Vec2Test, MinMaxFloat) {
    Vec2 mn = Vec2(3, 1).Min(2.f);
    EXPECT_FLOAT_EQ(mn.x, 2.f); EXPECT_FLOAT_EQ(mn.y, 1.f);
    Vec2 mx = Vec2(3, 1).Max(2.f);
    EXPECT_FLOAT_EQ(mx.x, 3.f); EXPECT_FLOAT_EQ(mx.y, 2.f);
}

TEST(Vec2Test, ClampVec) {
    Vec2 r = Vec2(5, -5).Clamp(Vec2(0, 0), Vec2(3, 3));
    EXPECT_FLOAT_EQ(r.x, 3.f); EXPECT_FLOAT_EQ(r.y, 0.f);
}

TEST(Vec2Test, ClampFloat) {
    Vec2 r = Vec2(5, -5).Clamp(0.f, 3.f);
    EXPECT_FLOAT_EQ(r.x, 3.f); EXPECT_FLOAT_EQ(r.y, 0.f);
}

TEST(Vec2Test, Lerp) {
    Vec2 r = Vec2(0, 0).Lerp(Vec2(10, 20), 0.5f);
    EXPECT_NEAR(r.x, 5.f, 1e-5); EXPECT_NEAR(r.y, 10.f, 1e-5);
}

TEST(Vec2Test, LerpBoundary) {
    Vec2 a(1, 2);
    Vec2 r0 = a.Lerp(Vec2(10, 20), 0.f);
    EXPECT_NEAR(r0.x, 1.f, 1e-5);
    Vec2 r1 = a.Lerp(Vec2(10, 20), 1.f);
    EXPECT_NEAR(r1.x, 10.f, 1e-5);
}

TEST(Vec2Test, LerpScalar) {
    Vec2 r = Vec2(0, 0).Lerp(10.f, 0.5f);
    EXPECT_NEAR(r.x, 5.f, 1e-5); EXPECT_NEAR(r.y, 5.f, 1e-5);
}

TEST(Vec2Test, DeltaAngle) {
    Vec2 r = Vec2(10, 350).DeltaAngle(Vec2(350, 10));
    EXPECT_NEAR(r.x, 20.f, 1e-3);
    EXPECT_NEAR(r.y, -20.f, 1e-3);
}

TEST(Vec2Test, DeltaAngleScalar) {
    Vec2 r = Vec2(10, 350).DeltaAngle(0.f);
    EXPECT_NEAR(r.x, 10.f, 1e-3);
    EXPECT_NEAR(r.y, -10.f, 1e-3);
}

TEST(Vec2Test, LerpAngle) {
    Vec2 r = Vec2(350, 0).LerpAngle(Vec2(10, 0), 0.5f);
    EXPECT_NEAR(r.y, 0.f, 1e-3);
}
