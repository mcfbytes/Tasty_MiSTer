// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/no_merge_rule.h"

#include <array>
#include <span>

#include "svc/types.h"

namespace mister::svc {

namespace {

constexpr auto kNoMergeRules = std::to_array<NoMergeRule>({
    {.match = {.vid = Vid{0x289B}, .pid = Pid{0x0057}, .arity = MatchArity::VidNotPid},
     .why = "req row 1: Raphnet, all PIDs EXCEPT 0x0057"},
    {.match = {.vid = Vid{0x0E8F}, .pid = Pid{0x3013}, .arity = MatchArity::VidAndPid},
     .why = "req row 2: Mayflash SNES 2-port adapter"},
    {.match = {.vid = Vid{0x16C0}, .pid = Pid{0x05E1}, .arity = MatchArity::VidAndPid},
     .why = "req row 3: XinMo XM-10 2-player encoder"},
    {.match = {.vid = Vid{0x045E}, .pid = Pid{0x02A1}, .arity = MatchArity::VidAndPid},
     .why = "req row 4: Xbox 360 wireless receiver"},
    {.match = {.vid = Vid{0x8282}, .pid = Pid{0x3201}, .arity = MatchArity::VidAndPid},
     .why = "req row 5: Irken JAMMA Expander / Mojo Retro"},
    {.match = {.vid = Vid{0x1209}, .pid = Pid{0xFACA}, .arity = MatchArity::VidAndPid},
     .why = "req row 6: ControllaBLE"},
    {.match = {.vid = Vid{0x16D0}, .pid = Pid{0x127E}, .arity = MatchArity::VidAndPid},
     .why = "req row 7: Reflex Adapt to USB"},
    {.match = {.vid = Vid{0x16D0}, .pid = Pid{0x1460}, .arity = MatchArity::VidAndPid},
     .why = "req row 8: Reflex Adapt Classic2USB"},
    {.match = {.vid = Vid{0x1209}, .pid = Pid{0x595A}, .arity = MatchArity::VidAndPid},
     .why = "req row 9: RetroZord adapter"},
});

consteval bool quirk_rows_are_vid_exact() {
    for (const NoMergeRule& r : kNoMergeRules) {
        if (r.match.vid == kAnyVid)
            return false;
    }
    return true;
}

static_assert(
    quirk_rows_are_vid_exact(),
    "a no-merge row's vid is EXACT at every MatchArity — kAnyVid is the "
    "ADMISSION table's notation only (req)"
);

}

std::span<const NoMergeRule> no_merge_rules() {
    return kNoMergeRules;
}

}
