#pragma once
// ---------------------------------------------------------------------------
// TestRunner.h — minimal zero-dependency unit test framework.
//
// Usage:
//   TEST("suite/name") { EXPECT(1 + 1 == 2); EXPECT_EQ(a, b); }
//   int main() { return TestRunner::Run(); }
//
// Output (stdout):
//   PASS  suite/name
//   FAIL  suite/name  line 42: expr text
//   ...
//   12 passed, 0 failed
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>
#include <string>

struct TestCase
{
    const char*           name;
    std::function<void()> fn;
};

namespace TestRunner
{
    // Registration list — filled by TEST() macros before main() runs.
    inline std::vector<TestCase>& Cases()
    {
        static std::vector<TestCase> v;
        return v;
    }

    // Per-test state written by EXPECT* macros.
    inline int&  FailCount() { static int n = 0; return n; }
    inline bool& InTest()    { static bool b = false; return b; }

    // Called by EXPECT* macros.
    inline void Check(bool cond, const char* expr, const char* file, int line)
    {
        if (!cond)
        {
            ++FailCount();
            // Extract just the filename from the full path for brevity.
            const char* slash = strrchr(file, '\\');
            if (!slash) slash = strrchr(file, '/');
            printf("        ASSERT FAILED  %s:%d  →  %s\n",
                   slash ? slash + 1 : file, line, expr);
        }
    }

    // Run all registered tests. Returns 0 if all pass, 1 otherwise.
    inline int Run()
    {
        int totalPass = 0, totalFail = 0;
        for (auto& tc : Cases())
        {
            FailCount() = 0;
            InTest()    = true;
            tc.fn();
            InTest() = false;

            if (FailCount() == 0)
            {
                printf("  PASS  %s\n", tc.name);
                ++totalPass;
            }
            else
            {
                printf("  FAIL  %s  (%d assertion(s) failed)\n",
                       tc.name, FailCount());
                ++totalFail;
            }
        }
        printf("\n%d passed, %d failed\n", totalPass, totalFail);
        return totalFail > 0 ? 1 : 0;
    }

    // Auto-registration helper — constructed before main().
    struct Registrar
    {
        Registrar(const char* name, std::function<void()> fn)
        {
            Cases().push_back({name, std::move(fn)});
        }
    };
} // namespace TestRunner

// ---------------------------------------------------------------------------
// Macros
// ---------------------------------------------------------------------------

// Two-level indirection forces __LINE__ to expand before token-pasting.
// Without this MSVC pastes the literal token __LINE__ and every TEST()
// in a translation unit gets the same name (_reg___LINE__).
#define _TR_CAT_(a, b) a##b
#define _TR_CAT(a, b)  _TR_CAT_(a, b)

// Define a test. Body runs as a lambda.
#define TEST(name) \
    static void _TR_CAT(_test_body_, __LINE__)(); \
    static TestRunner::Registrar _TR_CAT(_reg_, __LINE__)(name, _TR_CAT(_test_body_, __LINE__)); \
    static void _TR_CAT(_test_body_, __LINE__)()

// Basic assertions.
#define EXPECT(expr) \
    TestRunner::Check((expr), #expr, __FILE__, __LINE__)

#define EXPECT_EQ(a, b) \
    TestRunner::Check((a) == (b), #a " == " #b, __FILE__, __LINE__)

#define EXPECT_NE(a, b) \
    TestRunner::Check((a) != (b), #a " != " #b, __FILE__, __LINE__)

#define EXPECT_TRUE(expr)  EXPECT(expr)
#define EXPECT_FALSE(expr) TestRunner::Check(!(expr), "!" #expr, __FILE__, __LINE__)
