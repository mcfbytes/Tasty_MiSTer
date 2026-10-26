// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <tuple>

#include "app/diag_counters.h"
#include "app/fallback_counts.h"
#include "bay_record.h"
#include "hd_record.h"
#include "infra/json_out.h"
#include "infra/telemetry.h"
#include "rec_record.h"
#include "rg_record.h"
#include "record_ctx.h"
#include "rnd_record.h"
#include "tas_record.h"

namespace mister::fw {

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
    std::uint32_t open_saves_mounted = 0;
    std::uint32_t open_save_refusals = 0;
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
    o.field("osm", v.open_saves_mounted);
    o.field("osr", v.open_save_refusals);
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
        return View{
            .link = dc.link,
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
            .info_poll_pushes = dc.info_poll_pushes,
            .open_saves_mounted = dc.open_saves_mounted,
            .open_save_refusals = dc.open_save_refusals
        };
    }
};

using DiagRecords = std::
    tuple<SessRecord, BayRecord, RndRecord, TasRecord, RecRecord, HdRecord, RgRecord>;

}
