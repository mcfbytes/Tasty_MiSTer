// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <optional>
#include <tuple>

#include "app/diag_counters.h"
#include "app/avi_write_status.h"
#include "app/core_builder.h"
#include "app/encode_status.h"
#include "app/fallback_counts.h"
#include "app/hd_osd_status.h"
#include "app/rec_write_status.h"
#include "app/recorder_status.h"
#include "app/replay_status.h"
#include "infra/json_out.h"
#include "infra/opt_ref.h"
#include "infra/telemetry.h"
#include "reactor/round_timing.h"

namespace mister::fw {

struct RecordCtx {
    const app::DiagCounters& dc;
};

template <class R>
concept DiagRecord = requires(const R& r, const RecordCtx& c) {
    { R::kKey } -> std::convertible_to<infra::JsonName>;
    typename R::View;
    { r.sample(c) } -> std::same_as<std::optional<typename R::View>>;
};

template <class T, SeatTag S>
[[nodiscard]] std::optional<T> sample_published(const xthread::Telemetry<T, S>& cell) noexcept {
    T v{};
    if (cell.sample_into(v) == 0) return std::nullopt;
    return v;
}

template <class T, SeatTag S>
[[nodiscard]] std::optional<T> sample_published(
    infra::OptRef<const xthread::Telemetry<T, S>> cell) noexcept {
    if (!cell) return std::nullopt;
    return sample_published(*cell);
}

struct SessView {
    std::uint8_t link = 0;
    std::uint32_t core_shutdowns = 0;
    std::uint32_t recover_polls = 0;
    std::uint8_t stage_status_change = 0;
    std::uint32_t status_reply_refusals = 0;
    std::uint16_t last_error_code = 0;
    std::uint16_t last_error_site = 0;
    std::uint32_t last_error_detail = 0;
    std::uint32_t prefetch_park_timeouts = 0;
    std::uint32_t pause_expiries = 0;
    std::uint32_t stale_owner_steps = 0;
    std::uint32_t start_answer_drops = 0;
    std::uint32_t save_write_failures = 0;
    std::uint32_t start_timeouts = 0;
    std::uint32_t front_end_asked = 0;
    std::uint32_t front_end_failed = 0;
    std::uint32_t core_abandons = 0;
    std::uint32_t ini_write_failures = 0;
    std::uint32_t info_poll_pushes = 0;
};

constexpr void to_json(infra::JsonOut& o, const SessView& v) noexcept {
    o.field("link", v.link);
    o.field("shutdowns", v.core_shutdowns);
    o.field("recover_polls", v.recover_polls);
    o.field("ssc", v.stage_status_change);
    o.field("ssr", v.status_reply_refusals);
    o.field("err", v.last_error_code);
    o.field("err_site", v.last_error_site);
    o.field("err_detail", v.last_error_detail);
    o.field("pkto", v.prefetch_park_timeouts);
    o.field("pex", v.pause_expiries);
    o.field("stale", v.stale_owner_steps);
    o.field("srd", v.start_answer_drops);
    o.field("swf", v.save_write_failures);
    o.field("stto", v.start_timeouts);
    o.field("feask", v.front_end_asked);
    o.field("fefail", v.front_end_failed);
    o.field("cabn", v.core_abandons);
    o.field("iwf", v.ini_write_failures);
    o.field("ipush", v.info_poll_pushes);
}

struct SessRecord {
    template <class T>
    using Main = xthread::Telemetry<T, SeatTag::Main>;

    static constexpr infra::JsonName kKey{"sess"};
    using View = SessView;
    const Main<std::uint32_t>& pause_expiries;
    const Main<std::uint32_t>& recover_polls;
    const Main<std::uint32_t>& save_write_failures;
    const Main<app::FallbackCounts>& fallbacks;
    const Main<std::uint32_t>& ini_write_failures;
    [[nodiscard]] std::optional<View> sample(const RecordCtx& ctx) const noexcept {
        const app::DiagCounters& dc = ctx.dc;
        const app::FallbackCounts fb = fallbacks.sample().value;
        return View{.link = dc.link,
                    .core_shutdowns = dc.core_shutdowns,
                    .recover_polls = recover_polls.sample().value,
                    .stage_status_change = dc.stage_status_change,
                    .status_reply_refusals = dc.status_reply_refusals,
                    .last_error_code = dc.last_error_code,
                    .last_error_site = dc.last_error_site,
                    .last_error_detail = dc.last_error_detail,
                    .prefetch_park_timeouts = dc.prefetch_park_timeouts,
                    .pause_expiries = pause_expiries.sample().value,
                    .stale_owner_steps = dc.stale_owner_steps,
                    .start_answer_drops = dc.start_answer_drops,
                    .save_write_failures = save_write_failures.sample().value,
                    .start_timeouts = fb.start_timeouts,
                    .front_end_asked = fb.front_end_asked,
                    .front_end_failed = fb.front_end_failed,
                    .core_abandons = dc.core_abandons,
                    .ini_write_failures = ini_write_failures.sample().value,
                    .info_poll_pushes = dc.info_poll_pushes};
    }
};

struct TasRecord {
    static constexpr infra::JsonName kKey{"tas"};
    using View = app::ReplayStatus;
    const app::ReplayStatusCell& cell;
    [[nodiscard]] std::optional<View> sample(const RecordCtx&) const noexcept {
        return sample_published(cell);
    }
};

struct RecView {
    app::RecorderStatus capture{};
    app::EncodeStatus encode{};
    app::RecWriteStatus write{};
    app::AviWriteStatus avi{};
};

constexpr void to_json(infra::JsonOut& o, const RecView& v) noexcept {
    to_json(o, v.capture);
    to_json(o, v.encode);
    to_json(o, v.write);
    to_json(o, v.encode.video);
    to_json(o, v.avi);
}

struct RecRecord {
    static constexpr infra::JsonName kKey{"rec"};
    using View = RecView;
    const app::RecorderStatusCell& capture;
    const app::EncodeStatusCell& encode;
    const app::RecWriteStatusCell& write;
    const app::AviWriteStatusCell& avi;
    [[nodiscard]] std::optional<View> sample(const RecordCtx&) const noexcept {
        View v;
        if (capture.sample_into(v.capture) == 0) return std::nullopt;
        (void)encode.sample_into(v.encode);
        (void)write.sample_into(v.write);
        (void)avi.sample_into(v.avi);
        return v;
    }
};

struct HdRecord {
    static constexpr infra::JsonName kKey{"hd"};
    using View = app::HdOsdStatus;
    infra::OptRef<const app::HdOsdStatusCell> cell{};
    [[nodiscard]] std::optional<View> sample(const RecordCtx&) const noexcept {
        return sample_published(cell);
    }
};

struct RndView {
    std::uint32_t epoch = 0;
    std::uint32_t steady_rounds = 0;
    std::uint32_t steady_avg_us = 0;
    std::uint32_t steady_hist[reactor::kRoundBuckets] = {};
    std::uint32_t all_steady_rounds = 0;
    std::uint32_t all_steady_avg_us = 0;
    std::uint32_t all_steady_hist[reactor::kRoundBuckets] = {};
};

constexpr void to_json(infra::JsonOut& o, const RndView& v) noexcept {
    o.field("ep", v.epoch);
    o.field("sn", v.steady_rounds);
    o.field("sa", v.steady_avg_us);
    o.begin_array("sh");
    for (const std::uint32_t c : v.steady_hist)
        o.elem(c);
    o.end_array();
    o.field("asn", v.all_steady_rounds);
    o.field("asa", v.all_steady_avg_us);
    o.begin_array("ash");
    for (const std::uint32_t c : v.all_steady_hist)
        o.elem(c);
    o.end_array();
}

struct RndRecord {
    static constexpr infra::JsonName kKey{"rndh"};
    using View = RndView;
    const reactor::RoundTimingCell& cell;
    [[nodiscard]] std::optional<View> sample(const RecordCtx&) const noexcept {
        const std::optional<reactor::RoundTiming> t = sample_published(cell);
        if (!t) return std::nullopt;
        View v{.epoch = t->epoch,
               .steady_rounds = t->steady.n,
               .steady_avg_us = reactor::round_avg_us(t->steady),
               .all_steady_rounds = t->all_steady_rounds,
               .all_steady_avg_us = reactor::round_all_steady_avg_us(*t)};
        for (std::size_t b = 0; b < reactor::kRoundBuckets; ++b) {
            v.steady_hist[b] = t->steady.hist[b];
            v.all_steady_hist[b] = t->all_steady_hist[b];
        }
        return v;
    }
};

struct BayView {
    std::uint32_t builds = 0;
    std::uint32_t bay_full = 0;
    std::uint32_t frees = 0;
    std::uint32_t window_refusals = 0;
    std::uint32_t fed_refusals = 0;
};

constexpr void to_json(infra::JsonOut& o, const BayView& v) noexcept {
    o.field("build", v.builds);
    o.field("full", v.bay_full);
    o.field("free", v.frees);
    o.field("wref", v.window_refusals);
    o.field("fedref", v.fed_refusals);
}

struct BayRecord {
    static constexpr infra::JsonName kKey{"bay"};
    using View = BayView;
    const app::BayCountsCell& counts;
    [[nodiscard]] std::optional<View> sample(const RecordCtx& ctx) const noexcept {
        const app::BayCounts c = counts.sample().value;
        return View{.builds = c.builds,
                    .bay_full = c.bay_full,
                    .frees = c.frees,
                    .window_refusals = c.window_refusals,
                    .fed_refusals = ctx.dc.fed_refusals};
    }
};

using DiagRecords = std::tuple<SessRecord, BayRecord, RndRecord, TasRecord, RecRecord, HdRecord>;

}  // namespace mister::fw
