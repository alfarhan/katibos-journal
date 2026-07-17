// Tiny header-only test framework — no external deps, instant builds.
// Usage:  TEST(name){ CHECK(cond); CHECK_EQ_STR(a,b); CHECK_EQ_INT(a,b); }
// One TU defines main via:  int main(){ return mt_run(); }
#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include <functional>

struct MtCase { const char *name; std::function<void()> fn; };
inline std::vector<MtCase> &mt_cases() { static std::vector<MtCase> v; return v; }
inline int &mt_fail()   { static int f = 0; return f; }
inline int &mt_checks() { static int c = 0; return c; }

struct MtReg { MtReg(const char *n, std::function<void()> f) { mt_cases().push_back({n, f}); } };

#define TEST(name) \
    static void name(); \
    static MtReg mt_reg_##name(#name, name); \
    static void name()

#define CHECK(cond) do { mt_checks()++; if (!(cond)) { \
    mt_fail()++; printf("  FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_EQ_STR(a, b) do { mt_checks()++; std::string _a = (a), _b = (b); \
    if (_a != _b) { mt_fail()++; \
    printf("  FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a.c_str(), _b.c_str()); } } while (0)

#define CHECK_EQ_INT(a, b) do { mt_checks()++; long _a = (long)(a), _b = (long)(b); \
    if (_a != _b) { mt_fail()++; \
    printf("  FAIL %s:%d  %ld != %ld\n", __FILE__, __LINE__, _a, _b); } } while (0)

inline int mt_run() {
    for (auto &c : mt_cases()) {
        int before = mt_fail();
        c.fn();
        printf("%s %s\n", mt_fail() == before ? "ok  " : "FAIL", c.name);
    }
    printf("\n%d checks, %d failures\n", mt_checks(), mt_fail());
    return mt_fail() ? 1 : 0;
}
