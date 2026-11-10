// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/json_out.h"

namespace mister::fw {

struct SessView {
    std::uint8_t link = 0;
    std::uint32_t core_shutdowns = 0;
    std::uint32_t recover_polls = 0;
    std::uint8_t stage_status_change = 0;
    std::uint32_t status_reply_refusals = 0;
    std::uint16_t last_error_code = 0;
    infra::ErrorSite last_error_site{};
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

}
