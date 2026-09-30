// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace mister::testing {

inline int failures = 0;
inline int checks = 0;

inline bool skipped_under_qemu(const char* what) {
    if (std::getenv("TASTY_UNDER_QEMU") == nullptr) return false;
    std::printf("SKIP under qemu-user: %s\n", what);
    return true;
}

}  // namespace mister::testing

using mister::testing::checks;
using mister::testing::failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        ++::mister::testing::checks;                                    \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++::mister::testing::failures;                              \
        }                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                       \
    do {                                                                                     \
        ++::mister::testing::checks;                                                         \
        const long long tasty_va_ = static_cast<long long>(a);                               \
        const long long tasty_vb_ = static_cast<long long>(b);                               \
        if (tasty_va_ != tasty_vb_) {                                                        \
            std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, \
                        tasty_va_, tasty_vb_);                                               \
            ++::mister::testing::failures;                                                   \
        }                                                                                    \
    } while (0)

#define CHECK_EQ_STR(got, want)                                                    \
    do {                                                                           \
        ++::mister::testing::checks;                                               \
        const std::string tasty_g_ = (got);                                        \
        const std::string tasty_w_ = (want);                                       \
        if (tasty_g_ != tasty_w_) {                                                \
            std::printf("FAIL %s:%d:\n  got  %s\n  want %s\n", __FILE__, __LINE__, \
                        tasty_g_.c_str(), tasty_w_.c_str());                       \
            ++::mister::testing::failures;                                         \
        }                                                                          \
    } while (0)
