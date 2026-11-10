// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/diag_counters.h"
#include "infra/json_name.h"
#include "main_sess_cells.h"
#include "record_ctx.h"
#include "sess_view.h"

namespace mister::fw {

struct SessRecord {
    static constexpr infra::JsonName kKey{"sess"};
    using View = SessView;

    MainSessCells sess;

    [[nodiscard]] std::optional<View> sample(const RecordCtx& ctx) const noexcept {
        const app::DiagCounters& dc = ctx.dc;
        const app::FallbackCounts fb = sess.fallbacks.sample().value;
        return View{
            .link = dc.link.diag_byte(),
            .core_shutdowns = dc.core_shutdowns,
            .recover_polls = sess.recover_polls.sample().value,
            .stage_status_change = dc.stage_status_change,
            .status_reply_refusals = dc.status_reply_refusals,
            .last_error_code = dc.last_error_code,
            .last_error_site = dc.last_error_site,
            .last_error_detail = dc.last_error_detail,
            .prefetch_park_timeouts = dc.prefetch_park_timeouts,
            .pause_expiries = sess.pause_expiries.get(),
            .stale_owner_steps = dc.stale_owner_steps,
            .start_answer_drops = dc.start_answer_drops,
            .save_write_failures = sess.save_write_failures.get(),
            .start_timeouts = fb.start_timeouts,
            .front_end_asked = fb.front_end_asked,
            .front_end_failed = fb.front_end_failed,
            .core_abandons = dc.core_abandons,
            .ini_write_failures = sess.ini_write_failures.get(),
            .info_poll_pushes = dc.info_poll_pushes,
            .open_saves_mounted = dc.open_saves_mounted,
            .open_save_refusals = dc.open_save_refusals
        };
    }
};

}
