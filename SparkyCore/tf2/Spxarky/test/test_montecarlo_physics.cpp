// test_montecarlo_physics.cpp - Monte Carlo Physics simulations (Google Test)
#include <gtest/gtest.h>
#include "test_types.h"
#include "Utils/Math/Math.h"
#include <random>

TEST(MonteCarloTest, CalcAngleStability) {
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dist(-10000.f, 10000.f);

    int successes = 0;
    const int N = 10000;
    for (int i = 0; i < N; i++) {
        Vec3 start(dist(rng), dist(rng), dist(rng));
        Vec3 end(dist(rng), dist(rng), dist(rng));
        
        Vec3 ang = Math::CalcAngle(start, end);
        Vec3 fwd; Math::AngleVectors(ang, &fwd);
        
        Vec3 reconstructed = start + fwd * start.DistTo(end);
        if (end.DistTo(reconstructed) < 5.f) // Reasonable tolerance for large distances
            successes++;
    }
    EXPECT_GE(successes, N * 0.99f); // 99% success rate
}

TEST(MonteCarloTest, MatrixRoundTrip) {
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> angle_dist(-180.f, 180.f);
    std::uniform_real_distribution<float> pos_dist(-5000.f, 5000.f);

    float max_err = 0.f;
    for (int i = 0; i < 5000; i++) {
        Vec3 pos(pos_dist(rng), pos_dist(rng), pos_dist(rng));
        QAngle ang(angle_dist(rng), angle_dist(rng), angle_dist(rng));
        
        VMatrix mat; mat.SetupMatrixOrgAngles(pos, ang);
        Vector recovered_pos(mat[0][3], mat[1][3], mat[2][3]);
        
        float err = pos.DistTo(recovered_pos);
        if (err > max_err) max_err = err;
    }
    EXPECT_LT(max_err, 0.05f); // Very low error threshold
}

TEST(MonteCarloTest, RayToOBB_HitRate) {
    std::mt19937 rng(999);
    std::uniform_real_distribution<float> pos_dist(-500.f, 500.f);
    std::uniform_real_distribution<float> dir_dist(-1.f, 1.f);
    
    int hits = 0;
    const int N = 10000;
    
    Vec3 mins(-20.f, -20.f, -20.f);
    Vec3 maxs(20.f, 20.f, 20.f);
    VMatrix mat; mat.SetupMatrixOrgAngles({0,0,0}, {0,0,0});
    const matrix3x4& m34 = mat.As3x4();

    for (int i = 0; i < N; i++) {
        Vec3 start(pos_dist(rng), pos_dist(rng), pos_dist(rng));
        Vec3 dir(dir_dist(rng), dir_dist(rng), dir_dist(rng));
        dir.Normalize();
        
        Vec3 end = start + dir * 2000.f;
        if (Math::RayToOBB(start, dir, mins, maxs, m34))
            hits++;
    }
    // With this distribution, hitting a 40x40x40 box in a 1000x1000x1000 zone
    // is rare but should happen a few times.
    EXPECT_GT(hits, 0);
    EXPECT_LT(hits, N * 0.1f); // Should be less than 10% hit rate
}

TEST(MonteCarloTest, QuadraticSolver) {
    std::mt19937 rng(123);
    std::uniform_real_distribution<float> dist(-100.f, 100.f);
    
    int success = 0;
    const int N = 5000;
    for (int i = 0; i < N; i++) {
        float r1 = dist(rng), r2 = dist(rng);
        // (x - r1)(x - r2) = x^2 - (r1+r2)x + r1*r2
        float a = 1.f;
        float b = -(r1 + r2);
        float c = r1 * r2;
        
        auto roots = Math::SolveQuadratic(a, b, c);
        if (roots.size() == 2) {
            float err1 = std::min(abs(roots[0]-r1), abs(roots[0]-r2));
            float err2 = std::min(abs(roots[1]-r1), abs(roots[1]-r2));
            if (err1 < 0.1f && err2 < 0.1f)
                success++;
        }
    }
    EXPECT_GE(success, N * 0.95f);
}

TEST(MonteCarloTest, VectorStress) {
    std::mt19937 rng(777);
    std::uniform_real_distribution<float> dist(-5000.f, 5000.f);
    
    int ortho_success = 0;
    const int N = 10000;
    for (int i = 0; i < N; i++) {
        Vec3 a(dist(rng), dist(rng), dist(rng));
        Vec3 b(dist(rng), dist(rng), dist(rng));
        Vec3 c = a.Cross(b);
        
        float err_a = abs(a.Dot(c));
        float err_b = abs(b.Dot(c));
        // Relative tolerance
        float tol = a.Length() * c.Length() * 1e-4f;
        if (err_a <= tol && err_b <= tol)
            ortho_success++;
    }
    EXPECT_EQ(ortho_success, N);
}
