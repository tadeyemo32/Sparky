// main.cpp - Test suite entry point
#include "test_runner.h"

int main()
{
    printf("Spxarky Comprehensive Test Suite\n");
    printf("================================\n");
    printf("Includes: Vec2, Vec3, VMatrix, Math, FNV1A, ConfigVar,\n");
    printf("          Monte Carlo Physics, UI Component Logic\n");

    return TestRunner::Instance().RunAll();
}
