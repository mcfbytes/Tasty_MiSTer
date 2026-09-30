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

}  // namespace

const char* replay_end_name(ReplayEnd e) noexcept {
    const auto i = static_cast<std::size_t>(e);
    return i < kEndNames.size() ? kEndNames[i] : "?";
}

ReplayStatusText format_replay_status(const ReplayStatus& s) noexcept {
    std::array<char, ReplayStatusText::kBufSize> buf{};
    const int n = std::snprintf(
        buf.data(), buf.size(),
        "\"gen\":%u,\"lvl\":%u,\"end\":\"%s\",\"ref\":%u,\"frame\":%d,\"applied\":%d,"
        "\"writes\":%u,\"on_time\":%u,\"late\":%u,\"first_late\":%d,\"underruns\":%u,"
        "\"first_underrun\":%d,\"pre_epoch\":%u,\"skipped\":%u,\"held\":%u,\"drops\":%u,"
        "\"stale\":%u,\"link_err\":%u,\"min_us\":%u,\"max_us\":%u,\"epoch_us\":%u,\"epoch\":%u,"
        "\"blk_late\":%u,\"blk_max_us\":%u",
        static_cast<unsigned>(s.gen), static_cast<unsigned>(s.level), replay_end_name(s.end),
        static_cast<unsigned>(s.ref), s.movie_frame, s.applied_frame, s.writes, s.on_time, s.late,
        s.first_late_frame, s.underruns, s.first_underrun_frame, s.pre_epoch, s.skipped_edges,
        s.held_rounds, s.pad_drops, s.stale, s.link_errors, s.apply_min_us, s.apply_max_us,
        s.epoch_delay_us, s.epoch_edge, s.blk_late, s.blk_late_max_us);
    ReplayStatusText out{};
    const std::size_t len =
        n <= 0 ? 0 : std::min(static_cast<std::size_t>(n), ReplayStatusText::kCapacity);
    (void)out.assign(std::string_view(buf.data(), len));
    return out;
}

}  // namespace mister::app
