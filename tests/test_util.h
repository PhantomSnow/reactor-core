#pragma once

// 零依賴的迷你測試框架：TEST 註冊、CHECK 系列斷言，失敗時印出位置並計數。
// 刻意不用 GoogleTest，讓專案在沒有網路的環境也能直接建置。
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace testing_lite {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline int run_all() {
    int failed_tests = 0;
    for (const auto& test : registry()) {
        const int before = failures();
        test.fn();
        const bool ok = failures() == before;
        std::printf("[%s] %s\n", ok ? "  OK  " : " FAIL ", test.name);
        if (!ok) ++failed_tests;
    }
    std::printf("%zu test(s), %d failed\n", registry().size(), failed_tests);
    return failed_tests == 0 ? 0 : 1;
}

}  // namespace testing_lite

#define TEST(name)                                                              \
    static void name();                                                         \
    static ::testing_lite::Registrar name##_registrar(#name, name);             \
    static void name()

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            ++::testing_lite::failures();                                       \
        }                                                                       \
    } while (0)

#define CHECK_EQ(a, b)                                                          \
    do {                                                                        \
        const auto& va_ = (a);                                                  \
        const auto& vb_ = (b);                                                  \
        if (!(va_ == vb_)) {                                                    \
            std::printf("  %s:%d: CHECK_EQ(%s, %s) failed\n", __FILE__, __LINE__, #a, #b); \
            ++::testing_lite::failures();                                       \
        }                                                                       \
    } while (0)

#define TEST_MAIN() \
    int main() { return ::testing_lite::run_all(); }
