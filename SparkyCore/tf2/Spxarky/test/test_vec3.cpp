// test_vec3.cpp - Comprehensive Vec3 tests (Google Test)
#include <gtest/gtest.h>
#include "test_types.h"

// ==================== Constructors ====================
TEST(Vec3Test, DefaultConstructor) {
    Vec3 v;
    EXPECT_FLOAT_EQ(v.x, 0.f); EXPECT_FLOAT_EQ(v.y, 0.f); EXPECT_FLOAT_EQ(v.z, 0.f);
}

TEST(Vec3Test, ParameterizedConstructor) {
    Vec3 v(1.f, 2.f, 3.f);
    EXPECT_FLOAT_EQ(v.x, 1.f); EXPECT_FLOAT_EQ(v.y, 2.f); EXPECT_FLOAT_EQ(v.z, 3.f);
}

TEST(Vec3Test, CopyConstructor) {
    Vec3 a(4.f, 5.f, 6.f), b(a);
    EXPECT_FLOAT_EQ(b.x, 4.f); EXPECT_FLOAT_EQ(b.y, 5.f); EXPECT_FLOAT_EQ(b.z, 6.f);
}

TEST(Vec3Test, FromVec2Constructor) {
    Vec2 a(7.f, 8.f); Vec3 b(a);
    EXPECT_FLOAT_EQ(b.x, 7.f); EXPECT_FLOAT_EQ(b.y, 8.f); EXPECT_FLOAT_EQ(b.z, 0.f);
}

TEST(Vec3Test, PointerConstructor) {
    float arr[3] = { 9.f, 10.f, 11.f }; Vec3 v(arr);
    EXPECT_FLOAT_EQ(v.x, 9.f); EXPECT_FLOAT_EQ(v.y, 10.f); EXPECT_FLOAT_EQ(v.z, 11.f);
}

// ==================== Operators ====================
TEST(Vec3Test, Equality) { EXPECT_TRUE(Vec3(1,2,3) == Vec3(1,2,3)); EXPECT_FALSE(Vec3(1,2,3) == Vec3(1,2,4)); }
TEST(Vec3Test, Inequality) { EXPECT_TRUE(Vec3(1,2,3) != Vec3(1,2,4)); EXPECT_FALSE(Vec3(1,2,3) != Vec3(1,2,3)); }
TEST(Vec3Test, BoolOperator) { EXPECT_FALSE(static_cast<bool>(Vec3(0,0,0))); EXPECT_TRUE(static_cast<bool>(Vec3(0,0,1))); }
TEST(Vec3Test, IndexOperator) { Vec3 v(10,20,30); EXPECT_FLOAT_EQ(v[0],10.f); EXPECT_FLOAT_EQ(v[2],30.f); v[2]=99; EXPECT_FLOAT_EQ(v.z,99.f); }

TEST(Vec3Test, Add) { Vec3 r = Vec3(1,2,3)+Vec3(4,5,6); EXPECT_FLOAT_EQ(r.x,5.f); EXPECT_FLOAT_EQ(r.y,7.f); EXPECT_FLOAT_EQ(r.z,9.f); }
TEST(Vec3Test, Sub) { Vec3 r = Vec3(10,20,30)-Vec3(1,2,3); EXPECT_FLOAT_EQ(r.x,9.f); EXPECT_FLOAT_EQ(r.y,18.f); EXPECT_FLOAT_EQ(r.z,27.f); }
TEST(Vec3Test, Mul) { Vec3 r = Vec3(2,3,4)*Vec3(5,6,7); EXPECT_FLOAT_EQ(r.x,10.f); EXPECT_FLOAT_EQ(r.y,18.f); EXPECT_FLOAT_EQ(r.z,28.f); }
TEST(Vec3Test, Div) { Vec3 r = Vec3(10,20,30)/Vec3(2,4,5); EXPECT_FLOAT_EQ(r.x,5.f); EXPECT_FLOAT_EQ(r.y,5.f); EXPECT_FLOAT_EQ(r.z,6.f); }
TEST(Vec3Test, ScalarAdd) { Vec3 r = Vec3(1,2,3)+10.f; EXPECT_FLOAT_EQ(r.x,11.f); EXPECT_FLOAT_EQ(r.z,13.f); }
TEST(Vec3Test, ScalarSub) { Vec3 r = Vec3(10,20,30)-5.f; EXPECT_FLOAT_EQ(r.x,5.f); EXPECT_FLOAT_EQ(r.z,25.f); }
TEST(Vec3Test, ScalarMul) { Vec3 r = Vec3(2,3,4)*3.f; EXPECT_FLOAT_EQ(r.x,6.f); EXPECT_FLOAT_EQ(r.z,12.f); }
TEST(Vec3Test, ScalarDiv) { Vec3 r = Vec3(10,20,30)/5.f; EXPECT_FLOAT_EQ(r.x,2.f); EXPECT_FLOAT_EQ(r.z,6.f); }

TEST(Vec3Test, CompoundOps) {
    Vec3 v(1,2,3);
    v += Vec3(10,20,30); EXPECT_FLOAT_EQ(v.x, 11.f);
    v -= Vec3(1,2,3);   EXPECT_FLOAT_EQ(v.x, 10.f);
    v *= 2.f;            EXPECT_FLOAT_EQ(v.x, 20.f);
    v /= 4.f;            EXPECT_FLOAT_EQ(v.x, 5.f);
    v *= Vec3(2,1,1);   EXPECT_FLOAT_EQ(v.x, 10.f);
    v /= Vec3(2,1,1);   EXPECT_FLOAT_EQ(v.x, 5.f);
    v += 5.f;            EXPECT_FLOAT_EQ(v.x, 10.f);
    v -= 5.f;            EXPECT_FLOAT_EQ(v.x, 5.f);
}

// ==================== Methods ====================
TEST(Vec3Test, Zero) { Vec3 v(5,6,7); v.Zero(); EXPECT_FLOAT_EQ(v.x,0.f); EXPECT_FLOAT_EQ(v.z,0.f); }
TEST(Vec3Test, Set) { Vec3 v; v.Set(1,2,3); EXPECT_FLOAT_EQ(v.x,1.f); EXPECT_FLOAT_EQ(v.z,3.f); }
TEST(Vec3Test, To2D) { Vec3 r = Vec3(1,2,3).To2D(); EXPECT_FLOAT_EQ(r.z, 0.f); }
TEST(Vec3Test, Get2D) { Vec3 r = Vec3(1,2,3).Get2D(); EXPECT_FLOAT_EQ(r.z, 0.f); }

TEST(Vec3Test, Length) { EXPECT_NEAR(Vec3(1,2,2).Length(), 3.f, 1e-5); EXPECT_NEAR(Vec3(0,0,0).Length(), 0.f, 1e-5); }
TEST(Vec3Test, LengthSqr) { EXPECT_NEAR(Vec3(1,2,2).LengthSqr(), 9.f, 1e-5); }
TEST(Vec3Test, Length2D) { EXPECT_NEAR(Vec3(3,4,999).Length2D(), 5.f, 1e-5); }
TEST(Vec3Test, Length2DSqr) { EXPECT_NEAR(Vec3(3,4,999).Length2DSqr(), 25.f, 1e-5); }

TEST(Vec3Test, Normalize) {
    Vec3 v(3,4,0);
    float len = v.Normalize();
    EXPECT_NEAR(len, 5.f, 1e-4);
    EXPECT_NEAR(v.Length(), 1.f, 1e-4);
}

TEST(Vec3Test, Normalize2D) {
    Vec3 v(3,4,99); v.Normalize2D();
    EXPECT_NEAR(v.Length2D(), 1.f, 1e-4);
    EXPECT_FLOAT_EQ(v.z, 0.f);
}

TEST(Vec3Test, Normalized) {
    Vec3 n = Vec3(0,0,5).Normalized();
    EXPECT_NEAR(n.z, 1.f, 1e-4);
    EXPECT_NEAR(n.Length(), 1.f, 1e-4);
}

TEST(Vec3Test, Normalized2D) {
    Vec3 n = Vec3(3,4,99).Normalized2D();
    EXPECT_NEAR(n.Length2D(), 1.f, 1e-4);
}

TEST(Vec3Test, DistTo) { EXPECT_NEAR(Vec3(0,0,0).DistTo(Vec3(1,2,2)), 3.f, 1e-5); }
TEST(Vec3Test, DistTo2D) { EXPECT_NEAR(Vec3(0,0,0).DistTo2D(Vec3(3,4,999)), 5.f, 1e-5); }
TEST(Vec3Test, DistToSqr) { EXPECT_NEAR(Vec3(0,0,0).DistToSqr(Vec3(1,2,2)), 9.f, 1e-5); }
TEST(Vec3Test, DistTo2DSqr) { EXPECT_NEAR(Vec3(0,0,0).DistTo2DSqr(Vec3(3,4,999)), 25.f, 1e-5); }

TEST(Vec3Test, Dot) {
    EXPECT_NEAR(Vec3(1,0,0).Dot(Vec3(0,1,0)), 0.f, 1e-5);
    EXPECT_NEAR(Vec3(1,0,0).Dot(Vec3(1,0,0)), 1.f, 1e-5);
    EXPECT_NEAR(Vec3(2,3,4).Dot(Vec3(5,6,7)), 56.f, 1e-5);
}

TEST(Vec3Test, DotNormalized) {
    EXPECT_NEAR(Vec3(1,0,0).DotNormalized(Vec3(1,0,0)), 1.f, 1e-5);
    EXPECT_NEAR(Vec3(1,0,0).DotNormalized(Vec3(-1,0,0)), -1.f, 1e-5);
}

TEST(Vec3Test, DotSymmetry) {
    Vec3 a(1.5f,-2.3f,4.1f), b(-3.7f,0.8f,2.2f);
    EXPECT_NEAR(a.Dot(b), b.Dot(a), 1e-5);
}

TEST(Vec3Test, Cross) {
    Vec3 r = Vec3(1,0,0).Cross(Vec3(0,1,0));
    EXPECT_NEAR(r.x, 0.f, 1e-5); EXPECT_NEAR(r.y, 0.f, 1e-5); EXPECT_NEAR(r.z, 1.f, 1e-5);
}

TEST(Vec3Test, CrossAnticommutative) {
    Vec3 a(1,2,3), b(4,5,6);
    Vec3 ab = a.Cross(b), ba = b.Cross(a);
    EXPECT_NEAR(ab.x, -ba.x, 1e-5); EXPECT_NEAR(ab.y, -ba.y, 1e-5); EXPECT_NEAR(ab.z, -ba.z, 1e-5);
}

TEST(Vec3Test, CrossOrthogonal) {
    Vec3 a(1,2,3), b(4,5,6), c = a.Cross(b);
    EXPECT_NEAR(a.Dot(c), 0.f, 1e-3);
    EXPECT_NEAR(b.Dot(c), 0.f, 1e-3);
}

TEST(Vec3Test, IsZero) {
    EXPECT_TRUE(Vec3(0,0,0).IsZero());
    EXPECT_TRUE(Vec3(0.0005f,0.0005f,0.0005f).IsZero());
    EXPECT_FALSE(Vec3(1,0,0).IsZero());
}

TEST(Vec3Test, MinMaxScalar) { EXPECT_FLOAT_EQ(Vec3(3,1,2).Min(), 1.f); EXPECT_FLOAT_EQ(Vec3(3,1,2).Max(), 3.f); }
TEST(Vec3Test, MinMaxVec) {
    Vec3 mn = Vec3(3,1,5).Min(Vec3(2,4,3));
    EXPECT_FLOAT_EQ(mn.x, 2.f); EXPECT_FLOAT_EQ(mn.y, 1.f); EXPECT_FLOAT_EQ(mn.z, 3.f);
}

TEST(Vec3Test, Clamp) {
    Vec3 r = Vec3(10,-10,5).Clamp(Vec3(0,0,0), Vec3(7,7,7));
    EXPECT_FLOAT_EQ(r.x, 7.f); EXPECT_FLOAT_EQ(r.y, 0.f); EXPECT_FLOAT_EQ(r.z, 5.f);
}

TEST(Vec3Test, Lerp) {
    Vec3 r = Vec3(0,0,0).Lerp(Vec3(10,20,30), 0.5f);
    EXPECT_NEAR(r.x, 5.f, 1e-5); EXPECT_NEAR(r.y, 10.f, 1e-5); EXPECT_NEAR(r.z, 15.f, 1e-5);
}

TEST(Vec3Test, LerpBoundary) {
    Vec3 a(1,2,3);
    EXPECT_NEAR(a.Lerp(Vec3(11,22,33), 0.f).x, 1.f, 1e-5);
    EXPECT_NEAR(a.Lerp(Vec3(11,22,33), 1.f).x, 11.f, 1e-5);
}

TEST(Vec3Test, DeltaAngle) {
    Vec3 r = Vec3(10,350,0).DeltaAngle(Vec3(350,10,0));
    EXPECT_NEAR(r.x, 20.f, 1e-3); EXPECT_NEAR(r.y, -20.f, 1e-3);
}

TEST(Vec3Test, ToAngle) {
    Vec3 ang = Vec3(1,0,0).ToAngle();
    EXPECT_NEAR(ang.x, 0.f, 1e-3); EXPECT_NEAR(ang.y, 0.f, 1e-3);
}

TEST(Vec3Test, FromAngle) {
    Vec3 dir = Vec3(0,0,0).FromAngle();
    EXPECT_NEAR(dir.x, 1.f, 1e-4); EXPECT_NEAR(dir.y, 0.f, 1e-4); EXPECT_NEAR(dir.z, 0.f, 1e-4);
}

TEST(Vec3Test, ToAngleFromAngleRoundTrip) {
    Vec3 fwd(1,1,-1);
    Vec3 norm = fwd.Normalized();
    Vec3 ang = norm.ToAngle();
    Vec3 back = ang.FromAngle();
    EXPECT_NEAR(back.x, norm.x, 1e-3); EXPECT_NEAR(back.y, norm.y, 1e-3); EXPECT_NEAR(back.z, norm.z, 1e-3);
}
