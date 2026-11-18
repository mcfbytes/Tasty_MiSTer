// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/types.h"
#include "svc/vid_pid_key.h"

namespace mister::svc {

inline constexpr Vid kAllMiceVid{0xFFFF};
inline constexpr Pid kAllMicePid{0xFFFF};

struct SpinnerPolicy {
    Vid vid{};
    Pid pid{};

    [[nodiscard]] constexpr bool claims(VidPidKey device) const noexcept {
        const bool all_mice = vid == kAllMiceVid && pid == kAllMicePid;
        const bool exact = vid != kAnyVid
            && pid != kAnyPid
            && device.vid() == vid
            && device.pid() == pid;
        return all_mice || exact;
    }
};

}
