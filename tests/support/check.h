// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace mister::testing {

inline int failures = 0;
inline int checks = 0;

inline bool skipped_under_qemu(const char* what) {
    if (std::getenv("TASTY_UNDER_QEMU") == nullptr)
        return false;
    std::printf("SKIP under qemu-user: %s\n", what);
    return true;
}

template <class T>
concept StrongScalar = std::is_class_v<T>
    && std::is_aggregate_v<T>
    && requires(const T& t) { t.v; }
    && (std::is_integral_v<std::remove_cvref_t<decltype(std::declval<const T&>().v)>>
        || std::is_enum_v<std::remove_cvref_t<decltype(std::declval<const T&>().v)>>)
    && sizeof(T) == sizeof(std::declval<const T&>().v)
    && std::equality_comparable<T>;

inline void report_eq_failure(
    const char* sa,
    const char* sb,
    const char* file,
    int line,
    long long va,
    long long vb
) {
    std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", file, line, sa, sb, va, vb);
    ++failures;
}

template <StrongScalar T>
[[nodiscard]] constexpr T eq_operand(const T& a) noexcept {
    return a;
}

template <class A>
    requires(!StrongScalar<A> && requires(const A& a) { static_cast<long long>(a); })
[[nodiscard]] constexpr long long eq_operand(const A& a) noexcept {
    return static_cast<long long>(a);
}

template <StrongScalar T>
void check_eq(T a, T b, const char* sa, const char* sb, const char* file, int line) {
    ++checks;
    if (!(a == b))
        report_eq_failure(
            sa,
            sb,
            file,
            line,
            static_cast<long long>(a.v),
            static_cast<long long>(b.v)
        );
}

inline void check_eq(
    long long a,
    long long b,
    const char* sa,
    const char* sb,
    const char* file,
    int line
) {
    ++checks;
    if (a != b)
        report_eq_failure(sa, sb, file, line, a, b);
}

}

using mister::testing::checks;
using mister::testing::failures;

#define CHECK(cond) \
    do { \
        ++::mister::testing::checks; \
        if (!(cond)) { \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++::mister::testing::failures; \
        } \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        const auto tasty_va_ = ::mister::testing::eq_operand(a); \
        const auto tasty_vb_ = ::mister::testing::eq_operand(b); \
        ::mister::testing::check_eq(tasty_va_, tasty_vb_, #a, #b, __FILE__, __LINE__); \
    } while (0)

#define CHECK_EQ_STR(got, want) \
    do { \
        ++::mister::testing::checks; \
        const std::string tasty_g_ = (got); \
        const std::string tasty_w_ = (want); \
        if (tasty_g_ != tasty_w_) { \
            std::printf( \
                "FAIL %s:%d:\n  got  %s\n  want %s\n", \
                __FILE__, \
                __LINE__, \
                tasty_g_.c_str(), \
                tasty_w_.c_str() \
            ); \
            ++::mister::testing::failures; \
        } \
    } while (0)
