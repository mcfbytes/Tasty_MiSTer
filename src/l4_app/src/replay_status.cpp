// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/replay_status.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <string_view>

namespace mister::app {
namespace {

constexpr std::array<const char*, static_cast<std::size_t>(ReplayEnd::kCount)> kEndNames{
    "none",     "finished", "stopped", "superseded", "switch",
    "ref_lost", "no_ref",   "epoch",   "refused"};
static_assert(kEndNames.back() != nullptr, "every ReplayEnd has a name");

constexpr char kFmt[] =
    "\"gen\":%u,\"lvl\":%u,\"end\":\"%s\",\"ref\":%u,\"frame\":%d,\"applied\":%d,"
    "\"writes\":%u,\"on_time\":%u,\"late\":%u,\"first_late\":%d,\"underruns\":%u,"
    "\"first_underrun\":%d,\"pre_epoch\":%u,\"skipped\":%u,\"held\":%u,\"drops\":%u,"
    "\"stale\":%u,\"link_err\":%u,\"min_us\":%u,\"max_us\":%u,\"epoch_us\":%u,\"epoch\":%u,"
    "\"blk_late\":%u,\"blk_max_us\":%u,\"lost\":%u,\"delayed\":%u,\"gap_max_us\":%u,"
    "\"gap_frame\":%d,\"late_gap_us\":%u,\"late_into_us\":%u,\"late_edge_us\":%u,"
    "\"late_depth\":%u,\"depth_min\":%d,\"depth_min_frame\":%d";

consteval std::size_t worst_len() {
    std::size_t n = sizeof(kFmt) - 1, name = 0;
    for (const char* e : kEndNames)
        name = std::max(name, std::string_view(e).size());
    for (std::size_t i = 0; i + 1 < sizeof(kFmt); ++i)
        if (kFmt[i] == '%') n += (kFmt[i + 1] == 's' ? name : 11) - 2;
    return n;
}
static_assert(worst_len() <= ReplayStatusText::kCapacity, "the status text never clips");

}  // namespace

const char* replay_end_name(ReplayEnd e) noexcept {
    const auto i = static_cast<std::size_t>(e);
    return i < kEndNames.size() ? kEndNames[i] : "?";
}

const char* replay_end_sentence(ReplayEnd e, EpochFail fail) noexcept {
    switch (e) {
        case ReplayEnd::Superseded:
            return "a newer replay replaced this one; start the movie again if you still want it";
        case ReplayEnd::CoreSwitch:
            return "the core changed during the movie; play it again";
        case ReplayEnd::RefLost:
            return "tasty lost track of the core's frames during the movie; play it again";
        case ReplayEnd::NoReference:
            return "tasty cannot see this core's frames, so it cannot time a movie on it; use the "
                   "system's standard core";
        case ReplayEnd::EpochAmbiguous:
            if (fail == EpochFail::Untimed)
                return "the core's first frame came too late to time a negative --lead; pass "
                       "--lead 0 or higher, or play it again";
            return "the movie's first frame could not be placed on the core's frames, even after "
                   "reloading; play it again";
        case ReplayEnd::Refused:
            return "the replay was refused before it started; check the line above and try again";
        default:
            return "the replay stopped; play it again";
    }
}

ReplayStatusText format_replay_status(const ReplayStatus& s) noexcept {
    std::array<char, ReplayStatusText::kBufSize> buf{};
    const int n = std::snprintf(
        buf.data(), buf.size(), kFmt, static_cast<unsigned>(s.gen), static_cast<unsigned>(s.level),
        replay_end_name(s.end), static_cast<unsigned>(s.ref), s.movie_frame, s.applied_frame,
        s.writes, s.on_time, s.late, s.first_late_frame, s.underruns, s.first_underrun_frame,
        s.pre_epoch, s.skipped_edges, s.held_rounds, s.pad_drops, s.stale, s.link_errors,
        s.apply_min_us, s.apply_max_us, s.epoch_delay_us, s.epoch_edge, s.blk_late,
        s.blk_late_max_us, s.lost, s.delayed, s.gap_max_us, s.gap_frame, s.late_gap_us,
        s.late_into_us, s.late_edge_us, s.late_depth, s.depth_min, s.depth_min_frame);
    ReplayStatusText out{};
    const std::size_t len =
        n <= 0 ? 0 : std::min(static_cast<std::size_t>(n), ReplayStatusText::kCapacity);
    (void)out.assign(std::string_view(buf.data(), len));
    return out;
}

}  // namespace mister::app
