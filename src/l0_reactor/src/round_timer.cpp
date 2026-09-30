// SPDX-License-Identifier: GPL-3.0-or-later
#include "reactor/round_timer.h"

namespace mister::reactor {

namespace {

constexpr std::uint32_t kU32Max = 0xFFFF'FFFFu;

constexpr std::uint32_t sat_add(std::uint32_t a, std::uint64_t b) noexcept {
    const std::uint64_t s = static_cast<std::uint64_t>(a) + b;
    return s > kU32Max ? kU32Max : static_cast<std::uint32_t>(s);
}

constexpr std::uint64_t non_negative(std::int64_t v) noexcept {
    return v < 0 ? 0u : static_cast<std::uint64_t>(v);
}

}  // namespace

void RoundTimer::begin(std::int64_t t_wake_ns) noexcept {
    if (epoch_start_ns_ == 0) epoch_start_ns_ = t_wake_ns;
    round_start_ns_ = t_wake_ns;
    last_mark_ns_ = t_wake_ns;
    longest_ns_ = -1;
    longest_ = {};
    lifecycle_ = 0;
    in_round_ = true;
}

void RoundTimer::wake(std::int64_t latency_ns, std::uint64_t missed_ticks) noexcept {
    if (!in_round_) return;
    const std::uint64_t lat = non_negative(latency_ns);
    record_(acc_.wake, lat, {});
    acc_.tick_overruns = sat_add(acc_.tick_overruns, missed_ticks);
    if (lat > acc_.all_wake_max_ns) acc_.all_wake_max_ns = lat;
}

void RoundTimer::mark(RoundSegment seg, std::size_t index, std::int64_t now_ns) noexcept {
    if (!in_round_) return;
    const std::int64_t d = now_ns - last_mark_ns_;
    if (d > longest_ns_) {
        longest_ns_ = d;
        longest_ = RoundTiming::Cause{seg, static_cast<std::uint8_t>(index), 0, 0};
    }
    last_mark_ns_ = now_ns;
}

void RoundTimer::note_lifecycle(RoundLifecycle why) noexcept {
    lifecycle_ = static_cast<std::uint8_t>(lifecycle_ | static_cast<std::uint8_t>(why));
}

std::int64_t RoundTimer::end(std::int64_t t_end_ns) noexcept {
    const std::int64_t t = t_end_ns > 0 ? t_end_ns : last_mark_ns_;
    if (!in_round_) return t;
    mark(RoundSegment::Tail, 0, t);
    const std::uint64_t dur = non_negative(t - round_start_ns_);
    RoundTiming::Cause cause = longest_;
    cause.lifecycle = lifecycle_;
    if (lifecycle_ != 0) {
        record_(acc_.lifecycle, dur, cause);
        acc_.all_life_rounds = sat_add(acc_.all_life_rounds, 1);
        if (dur > acc_.all_life_max_ns) {
            acc_.all_life_max_ns = dur;
            acc_.all_life_cause = cause;
        }
    } else {
        record_(acc_.steady, dur, cause);
        acc_.all_steady_rounds = sat_add(acc_.all_steady_rounds, 1);
        if (dur > kOverNs) acc_.all_steady_over_1ms = sat_add(acc_.all_steady_over_1ms, 1);
        if (dur > acc_.all_steady_max_ns) {
            acc_.all_steady_max_ns = dur;
            acc_.all_steady_cause = cause;
            acc_.steady_max_epoch = acc_.epoch + 1;
        }
    }
    in_round_ = false;
    if (t - epoch_start_ns_ >= kEpochNs) close_epoch_(t);
    return t;
}

void RoundTimer::flush(std::int64_t now_ns) noexcept {
    in_round_ = false;
    if (epoch_start_ns_ != 0) close_epoch_(now_ns);
}

void RoundTimer::record_(RoundTiming::Dist& d, std::uint64_t ns, RoundTiming::Cause c) noexcept {
    if (d.n == 0 || ns < d.min_ns) d.min_ns = ns;
    d.n = sat_add(d.n, 1);
    d.sum_ns += ns;
    if (ns > d.max_ns) {
        d.max_ns = ns;
        d.max_cause = c;
    }
    if (ns > kOverNs) d.over_1ms = sat_add(d.over_1ms, 1);
    std::uint32_t& h = d.hist[round_bucket(ns)];
    h = sat_add(h, 1);
}

void RoundTimer::close_epoch_(std::int64_t now_ns) noexcept {
    acc_.epoch_ns = non_negative(now_ns - epoch_start_ns_);
    acc_.epoch = sat_add(acc_.epoch, 1);
    cell_.publish(acc_);
    acc_.steady = RoundTiming::Dist{};
    acc_.lifecycle = RoundTiming::Dist{};
    acc_.wake = RoundTiming::Dist{};
    epoch_start_ns_ = now_ns;
}

}  // namespace mister::reactor
