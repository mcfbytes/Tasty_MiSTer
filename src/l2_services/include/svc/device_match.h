// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/types.h"

namespace mister::svc {

enum class MatchArity : std::uint8_t {
    VidOnly,
    VidAndPid,
    VidNotPid,
};

struct DeviceMatch {
    Vid vid;
    Pid pid;
    MatchArity arity;

    constexpr bool matches(Vid v, Pid p) const {
        switch (arity) {
            case MatchArity::VidOnly:
                return v == vid;
            case MatchArity::VidAndPid:
                return v == vid && p == pid;
            case MatchArity::VidNotPid:
                return v == vid && p != pid;
        }
        return false;
    }
};

}  // namespace mister::svc
