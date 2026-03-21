// test_vmatrix.cpp - VMatrix tests (Google Test)
#include <gtest/gtest.h>
#include "test_types.h"

TEST(VMatrixTest, IdentitySetup) {
    VMatrix mat; mat.SetupMatrixOrgAngles(Vector(0,0,0), QAngle(0,0,0));
    EXPECT_NEAR(mat[0][0], 1.f, 1e-5); EXPECT_NEAR(mat[1][1], 1.f, 1e-5); EXPECT_NEAR(mat[2][2], 1.f, 1e-5);
    EXPECT_NEAR(mat[0][1], 0.f, 1e-5); EXPECT_NEAR(mat[1][0], 0.f, 1e-5);
    EXPECT_NEAR(mat[0][3], 0.f, 1e-5); EXPECT_NEAR(mat[3][3], 1.f, 1e-5);
}

TEST(VMatrixTest, TranslationOnly) {
    VMatrix mat; mat.SetupMatrixOrgAngles(Vector(10,20,30), QAngle(0,0,0));
    EXPECT_NEAR(mat[0][3], 10.f, 1e-5); EXPECT_NEAR(mat[1][3], 20.f, 1e-5); EXPECT_NEAR(mat[2][3], 30.f, 1e-5);
}

TEST(VMatrixTest, VMul4x3_Identity) {
    VMatrix mat; mat.SetupMatrixOrgAngles(Vector(0,0,0), QAngle(0,0,0));
    Vector r = mat.VMul4x3(Vector(5,10,15));
    EXPECT_NEAR(r.x, 5.f, 1e-4); EXPECT_NEAR(r.y, 10.f, 1e-4); EXPECT_NEAR(r.z, 15.f, 1e-4);
}

TEST(VMatrixTest, VMul4x3_Translation) {
    VMatrix mat; mat.SetupMatrixOrgAngles(Vector(100,200,300), QAngle(0,0,0));
    Vector r = mat.VMul4x3(Vector(1,2,3));
    EXPECT_NEAR(r.x, 101.f, 1e-4); EXPECT_NEAR(r.y, 202.f, 1e-4); EXPECT_NEAR(r.z, 303.f, 1e-4);
}

TEST(VMatrixTest, VMul4x3_Rotation90Yaw) {
    VMatrix mat; mat.SetupMatrixOrgAngles(Vector(0,0,0), QAngle(0,90,0));
    Vector r = mat.VMul4x3(Vector(1,0,0));
    EXPECT_NEAR(r.x, 0.f, 1e-4); EXPECT_NEAR(r.y, 1.f, 1e-4); EXPECT_NEAR(r.z, 0.f, 1e-4);
}

TEST(VMatrixTest, LocalToWorld_WorldToLocal_RoundTrip) {
    VMatrix mat; mat.SetupMatrixOrgAngles(Vector(50,100,-30), QAngle(30,45,10));
    Vector local(5,10,15);
    Vector recovered = mat.WorldToLocal(mat.LocalToWorld(local));
    EXPECT_NEAR(recovered.x, local.x, 1e-2); EXPECT_NEAR(recovered.y, local.y, 1e-2); EXPECT_NEAR(recovered.z, local.z, 1e-2);
}

TEST(VMatrixTest, VMul3x3_RoundTrip) {
    VMatrix mat; mat.SetupMatrixOrgAngles(Vector(0,0,0), QAngle(45,60,30));
    Vector v(3,4,5);
    Vector recovered = mat.VMul3x3Transpose(mat.VMul3x3(v));
    EXPECT_NEAR(recovered.x, v.x, 1e-2); EXPECT_NEAR(recovered.y, v.y, 1e-2); EXPECT_NEAR(recovered.z, v.z, 1e-2);
}

TEST(VMatrixTest, RotationPreservesLength) {
    VMatrix mat; mat.SetupMatrixOrgAngles(Vector(0,0,0), QAngle(35,72,-15));
    Vector v(3,4,5);
    EXPECT_NEAR(mat.VMul3x3(v).Length(), v.Length(), 1e-3);
}

TEST(VMatrixTest, As3x4) {
    VMatrix mat; mat.SetupMatrixOrgAngles(Vector(1,2,3), QAngle(10,20,30));
    const matrix3x4& m34 = mat.As3x4();
    EXPECT_NEAR(m34[0][3], 1.f, 1e-5); EXPECT_NEAR(m34[1][3], 2.f, 1e-5); EXPECT_NEAR(m34[2][3], 3.f, 1e-5);
}

TEST(VMatrixTest, RotationRoundTrips) {
    VMatrix mat; mat.SetupMatrixOrgAngles(Vector(0,0,0), QAngle(15,25,35));
    Vector v(7,-3,2);
    Vector r = mat.WorldToLocalRotation(mat.LocalToWorldRotation(v));
    EXPECT_NEAR(r.x, v.x, 1e-2); EXPECT_NEAR(r.y, v.y, 1e-2); EXPECT_NEAR(r.z, v.z, 1e-2);
}
