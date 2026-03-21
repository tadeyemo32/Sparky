cmake_minimum_required(VERSION 3.14)
project(SpxarkyTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Fetch Google Test ──────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# ── Test executable ────────────────────────────────────────────────
add_executable(spxarky_tests
    test_vec2.cpp
    test_vec3.cpp
    test_vmatrix.cpp
    test_math.cpp
    test_fnv1a.cpp
    test_ui_logic.cpp
    test_ui_components.cpp
    test_montecarlo_physics.cpp
    test_misc.cpp
    test_aimbot_melee_logic.cpp
    test_spy_logic.cpp
    test_optimizations.cpp
    test_skin_changer.cpp
)

target_link_libraries(spxarky_tests PRIVATE GTest::gtest_main)
target_include_directories(spxarky_tests PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Spxarky/src
)

# UBSan in debug builds
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(spxarky_tests PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

include(GoogleTest)
gtest_discover_tests(spxarky_tests)
