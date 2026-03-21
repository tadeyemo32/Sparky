#pragma once
// Lightweight C++ test runner - zero dependencies
// Usage: TEST(name) { ASSERT_TRUE(cond); ASSERT_EQ(a,b); ASSERT_NEAR(a,b,eps); }
//        At end of main(): return TestRunner::Instance().RunAll();

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#define COLOR_RED ""
#define COLOR_GREEN ""
#define COLOR_YELLOW ""
#define COLOR_RESET ""
#else
#define COLOR_RED "\033[1;31m"
#define COLOR_GREEN "\033[1;32m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_RESET "\033[0m"
#endif

struct TestCase
{
    std::string name;
    std::function<void()> func;
};

class TestRunner
{
public:
    static TestRunner& Instance()
    {
        static TestRunner instance;
        return instance;
    }

    void Register(const char* name, std::function<void()> func)
    {
        m_tests.push_back({ name, func });
    }

    int RunAll()
    {
        int passed = 0, failed = 0;
        printf("\n========================================\n");
        printf("  Running %zu tests...\n", m_tests.size());
        printf("========================================\n\n");

        for (auto& test : m_tests)
        {
            m_currentFailed = false;
            printf("  [RUN ] %s\n", test.name.c_str());
            try
            {
                test.func();
            }
            catch (const std::exception& e)
            {
                printf("    %sEXCEPTION: %s%s\n", COLOR_RED, e.what(), COLOR_RESET);
                m_currentFailed = true;
            }
            catch (...)
            {
                printf("    %sUNKNOWN EXCEPTION%s\n", COLOR_RED, COLOR_RESET);
                m_currentFailed = true;
            }

            if (m_currentFailed)
            {
                printf("  [%sFAIL%s] %s\n\n", COLOR_RED, COLOR_RESET, test.name.c_str());
                failed++;
            }
            else
            {
                printf("  [%sPASS%s] %s\n\n", COLOR_GREEN, COLOR_RESET, test.name.c_str());
                passed++;
            }
        }

        printf("========================================\n");
        if (failed == 0)
            printf("  %sAll %d tests PASSED%s\n", COLOR_GREEN, passed, COLOR_RESET);
        else
            printf("  %s%d/%d tests PASSED, %d FAILED%s\n", COLOR_RED, passed, passed + failed, failed, COLOR_RESET);
        printf("========================================\n\n");

        return failed;
    }

    void Fail(const char* file, int line, const char* msg)
    {
        printf("    %sASSERT FAILED%s at %s:%d\n", COLOR_RED, COLOR_RESET, file, line);
        printf("      %s\n", msg);
        m_currentFailed = true;
    }

    bool m_currentFailed = false;

private:
    std::vector<TestCase> m_tests;
};

struct TestRegister
{
    TestRegister(const char* name, std::function<void()> func)
    {
        TestRunner::Instance().Register(name, func);
    }
};

#define TEST(name) \
    void test_##name(); \
    static TestRegister reg_##name(#name, test_##name); \
    void test_##name()

#define ASSERT_TRUE(cond) \
    do { if (!(cond)) { \
        TestRunner::Instance().Fail(__FILE__, __LINE__, "ASSERT_TRUE(" #cond ") failed"); \
        return; \
    } } while(0)

#define ASSERT_FALSE(cond) \
    do { if ((cond)) { \
        TestRunner::Instance().Fail(__FILE__, __LINE__, "ASSERT_FALSE(" #cond ") failed"); \
        return; \
    } } while(0)

#define ASSERT_EQ(a, b) \
    do { auto _a = (a); auto _b = (b); if (_a != _b) { \
        char _buf[256]; snprintf(_buf, sizeof(_buf), "ASSERT_EQ(" #a ", " #b ") failed: got %s vs %s", \
            std::to_string(_a).c_str(), std::to_string(_b).c_str()); \
        TestRunner::Instance().Fail(__FILE__, __LINE__, _buf); \
        return; \
    } } while(0)

#define ASSERT_NEQ(a, b) \
    do { auto _a = (a); auto _b = (b); if (_a == _b) { \
        char _buf[256]; snprintf(_buf, sizeof(_buf), "ASSERT_NEQ(" #a ", " #b ") failed: both are %s", \
            std::to_string(_a).c_str()); \
        TestRunner::Instance().Fail(__FILE__, __LINE__, _buf); \
        return; \
    } } while(0)

#define ASSERT_NEAR(a, b, eps) \
    do { double _a = (double)(a); double _b = (double)(b); double _e = (double)(eps); \
        if (std::abs(_a - _b) > _e) { \
        char _buf[256]; snprintf(_buf, sizeof(_buf), "ASSERT_NEAR(" #a ", " #b ", " #eps ") failed: |%.10g - %.10g| = %.10g > %.10g", \
            _a, _b, std::abs(_a - _b), _e); \
        TestRunner::Instance().Fail(__FILE__, __LINE__, _buf); \
        return; \
    } } while(0)

#define ASSERT_GT(a, b) \
    do { auto _a = (a); auto _b = (b); if (!(_a > _b)) { \
        char _buf[256]; snprintf(_buf, sizeof(_buf), "ASSERT_GT(" #a ", " #b ") failed: %s <= %s", \
            std::to_string(_a).c_str(), std::to_string(_b).c_str()); \
        TestRunner::Instance().Fail(__FILE__, __LINE__, _buf); \
        return; \
    } } while(0)

#define ASSERT_LT(a, b) \
    do { auto _a = (a); auto _b = (b); if (!(_a < _b)) { \
        char _buf[256]; snprintf(_buf, sizeof(_buf), "ASSERT_LT(" #a ", " #b ") failed: %s >= %s", \
            std::to_string(_a).c_str(), std::to_string(_b).c_str()); \
        TestRunner::Instance().Fail(__FILE__, __LINE__, _buf); \
        return; \
    } } while(0)

#define ASSERT_GTE(a, b) \
    do { auto _a = (a); auto _b = (b); if (!(_a >= _b)) { \
        char _buf[256]; snprintf(_buf, sizeof(_buf), "ASSERT_GTE(" #a ", " #b ") failed: %s < %s", \
            std::to_string(_a).c_str(), std::to_string(_b).c_str()); \
        TestRunner::Instance().Fail(__FILE__, __LINE__, _buf); \
        return; \
    } } while(0)
