// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <optional>
#include <tuple>

#include "app/diag_counters.h"
#include "app/avi_write_status.h"
#include "app/encode_status.h"
#include "app/fallback_counts.h"
#include "app/hd_osd_status.h"
#include "app/rec_write_status.h"
#include "app/recorder_status.h"
#include "app/replay_status.h"
#include "infra/json_out.h"
#include "infra/telemetry.h"

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
[[nodiscard]] std::optional<T> sample_published(const xthread::Telemetry<T, S>* cell) noexcept {
    if (cell == nullptr) return std::nullopt;
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
}

struct SessRecord {
    template <class T>
    using Unbound = xthread::Telemetry<T, SeatTag::Unbound>;

    static constexpr infra::JsonName kKey{"sess"};
    using View = SessView;
    const Unbound<std::uint32_t>& pause_expiries;
    const Unbound<std::uint32_t>& recover_polls;
    const Unbound<std::uint32_t>& save_write_failures;
    const Unbound<app::FallbackCounts>& fallbacks;
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
                    .core_abandons = dc.core_abandons};
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
    const app::HdOsdStatusCell* cell = nullptr;
    [[nodiscard]] std::optional<View> sample(const RecordCtx&) const noexcept {
        return sample_published(cell);
    }
};

using DiagRecords = std::tuple<SessRecord, TasRecord, RecRecord, HdRecord>;

}  // namespace mister::fw
