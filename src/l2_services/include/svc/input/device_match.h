// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/input/spinner_policy.h"
#include "svc/types.h"
#include "svc/input/vid_pid_key.h"

namespace mister::svc::input {

enum class MatchArity : std::uint8_t {
    VidOnly,
    VidAndPid,
    VidNotPid,
};

struct DeviceMatch {
    Vid vid;
    Pid pid;
    MatchArity arity;

    constexpr bool matches(VidPidKey device) const {
        switch (arity) {
            case MatchArity::VidOnly:
                return device.vid() == vid;
            case MatchArity::VidAndPid:
                return device.vid() == vid && device.pid() == pid;
            case MatchArity::VidNotPid:
                return device.vid() == vid && device.pid() != pid;
        }
        return false;
    }
};

}
