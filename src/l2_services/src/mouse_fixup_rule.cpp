// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/mouse_fixup_rule.h"

#include <optional>

#include "svc/types.h"

namespace mister::svc {

namespace {

constexpr MouseFixupRule kMouseFixupRules[] = {

    {DeviceMatch{Vid{0x32BE}, Pid{0x1420}, MatchArity::VidAndPid},
     QuirkId::Mssp,
     "req Arcade Spinner TS-BSP01/Atari, prediv 3"},
};

consteval bool mouse_fixup_rows_are_vid_exact() {
    for (const MouseFixupRule& r : kMouseFixupRules) {
        if (r.match.vid == kAnyVid)
            return false;
    }
    return true;
}

static_assert(
    mouse_fixup_rows_are_vid_exact(),
    "a mouse-fixup row's vid is EXACT at every MatchArity — kAnyVid is the "
    "ADMISSION table's notation only, and a zero-VID row would match every "
    "/dev/input/mouseN node (req)"
);

}

std::optional<MouseFixupRule> find_mouse_fixup(Vid vid, Pid pid) {
    for (const MouseFixupRule& r : kMouseFixupRules) {
        if (r.match.matches(vid, pid))
            return r;
    }
    return std::nullopt;
}

}
