// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/types.h"

namespace mister::svc {

inline constexpr Vid kAllMiceVid{0xFFFF};
inline constexpr Pid kAllMicePid{0xFFFF};

struct SpinnerPolicy {
    Vid vid{};
    Pid pid{};

    [[nodiscard]] constexpr bool claims(Vid v, Pid p) const noexcept {
        const bool all_mice = vid == kAllMiceVid && pid == kAllMicePid;
        const bool exact = vid != kAnyVid && pid != kAnyPid && v == vid && p == pid;
        return all_mice || exact;
    }
};

}
