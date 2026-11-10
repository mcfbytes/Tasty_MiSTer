// SPDX-License-Identifier: GPL-3.0-or-later
#include "reactor/round_timer.h"

#include <chrono>
#include <limits>

namespace mister::reactor {

namespace {

constexpr std::uint32_t kU32Max = std::numeric_limits<std::uint32_t>::max();

constexpr std::uint32_t sat_add(std::uint32_t a, std::uint64_t b) noexcept {
    const std::uint64_t s = static_cast<std::uint64_t>(a) + b;
    return s > kU32Max ? kU32Max : static_cast<std::uint32_t>(s);
}

constexpr std::uint64_t non_negative(std::int64_t v) noexcept {
    return v < 0 ? 0u : static_cast<std::uint64_t>(v);
}

}

void RoundTimer::begin(std::int64_t t_wake_ns) noexcept {
    if (epoch_start_ns_ == 0)
        epoch_start_ns_ = t_wake_ns;
    round_start_ns_ = t_wake_ns;
    last_mark_ns_ = t_wake_ns;
    longest_ns_ = -1;
    longest_blame_ = {};
    lifecycle_ = {};
    in_round_ = true;
    recorder_.begin(MonoInstant{std::chrono::nanoseconds{t_wake_ns}});
}

void RoundTimer::wake(std::int64_t latency_ns, std::uint64_t missed_ticks) noexcept {
    if (!in_round_)
        return;
    const std::uint64_t lat = non_negative(latency_ns);
    record_(acc_.wake, lat, {});
    acc_.tick_overruns = sat_add(acc_.tick_overruns, missed_ticks);
    if (lat > acc_.all_wake_max_ns)
        acc_.all_wake_max_ns = lat;
    recorder_.wake(round_span(std::chrono::nanoseconds{latency_ns}));
}

void RoundTimer::mark(RoundSegment seg, ServiceSlot row, std::int64_t now_ns) noexcept {
    if (!in_round_)
        return;
    const std::int64_t d = now_ns - last_mark_ns_;
    const RoundTiming::Blame at{seg, row};
    if (d > longest_ns_) {
        longest_ns_ = d;
        longest_blame_ = at;
    }
    recorder_.mark(at, round_span(std::chrono::nanoseconds{d}));
    last_mark_ns_ = now_ns;
}

void RoundTimer::mark(RoundSegment seg, std::int64_t now_ns) noexcept {
    mark(seg, ServiceSlot{}, now_ns);
}

void RoundTimer::note_lifecycle(RoundLifecycle why) noexcept {
    lifecycle_ = lifecycle_.with(why);
}

std::int64_t RoundTimer::end(std::int64_t t_end_ns) noexcept {
    const std::int64_t t = t_end_ns > 0 ? t_end_ns : last_mark_ns_;
    if (!in_round_)
        return t;
    mark(RoundSegment::Tail, t);
    const std::uint64_t dur = non_negative(t - round_start_ns_);
    RoundTiming::Blame blame = longest_blame_;
    blame.lifecycle_marks = lifecycle_;
    if (!lifecycle_.empty()) {
        record_(acc_.lifecycle, dur, blame);
        acc_.all_life_rounds = sat_add(acc_.all_life_rounds, 1);
        if (dur > acc_.all_life_max_ns) {
            acc_.all_life_max_ns = dur;
            acc_.all_life_blame = blame;
        }
    } else {
        std::uint32_t& h = acc_.all_steady_hist[record_(acc_.steady, dur, blame)];
        h = sat_add(h, 1);
        if (acc_.all_steady_rounds != kU32Max)
            acc_.all_steady_sum_ns += dur;
        acc_.all_steady_rounds = sat_add(acc_.all_steady_rounds, 1);
        if (dur > kOverNs)
            acc_.all_steady_over_1ms = sat_add(acc_.all_steady_over_1ms, 1);
        if (dur > acc_.all_steady_max_ns) {
            acc_.all_steady_max_ns = dur;
            acc_.all_steady_blame = blame;
            acc_.steady_max_epoch = acc_.epoch + 1;
        }
    }
    const MonoInstant finish{std::chrono::nanoseconds{t}};
    if (dur > kOverNs)
        recorder_.record(finish, blame);
    recorder_.end(finish, blame);
    in_round_ = false;
    if (t - epoch_start_ns_ >= kEpochNs)
        close_epoch_(t);
    return t;
}

void RoundTimer::flush(std::int64_t now_ns) noexcept {
    in_round_ = false;
    if (epoch_start_ns_ != 0)
        close_epoch_(now_ns);
}

std::size_t RoundTimer::record_(
    RoundTiming::Dist& d,
    std::uint64_t ns,
    RoundTiming::Blame blame
) noexcept {
    if (d.n == 0 || ns < d.min_ns)
        d.min_ns = ns;
    d.n = sat_add(d.n, 1);
    d.sum_ns += ns;
    if (ns > d.max_ns) {
        d.max_ns = ns;
        d.max_blame = blame;
    }
    if (ns > kOverNs)
        d.over_1ms = sat_add(d.over_1ms, 1);
    const std::size_t b = round_bucket(ns);
    d.hist[b] = sat_add(d.hist[b], 1);
    return b;
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

}
