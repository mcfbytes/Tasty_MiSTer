// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/bay_counts.h"
#include "app/sector_grant.h"
#include "app/osd_route.h"
#include "app/sector_route.h"
#include "app/sys_route.h"
#include "infra/opt_ref.h"
#include "infra/json_out.h"
#include "record_ctx.h"

namespace mister::fw {

struct BayView {
    std::uint32_t builds = 0;
    std::uint32_t bay_full = 0;
    std::uint32_t frees = 0;
    std::uint32_t window_refusals = 0;
    std::uint32_t fed_refusals = 0;
    std::uint32_t pad_grants = 0;
    std::uint32_t pad_refusals = 0;
    std::uint32_t pads_on_lw = 0;
    std::uint32_t sector_grants = 0;
    std::uint32_t sector_refusals = 0;
    std::uint32_t lease_holds = 0;
    std::uint32_t lease_long_holds = 0;
    app::SectorRoute cd{};
    std::uint32_t osd_grants = 0;
    std::uint32_t osd_refusals = 0;
    app::OsdRoute osd{};
    std::uint32_t osd_page_rows_link = 0;
    std::uint32_t osd_status_rows_link = 0;
    std::uint32_t card_grants = 0;
    std::uint32_t card_refusals = 0;
    std::uint32_t bridge_refusals = 0;
    std::uint32_t sys_grants = 0;
    std::uint32_t sys_refusals = 0;
    std::uint32_t sys_pin_refusals = 0;
    app::SysRoute sys{};
    std::uint32_t status_lw = 0;
    std::uint32_t status_lw_adopts = 0;
    std::uint32_t status_lw_stale = 0;
    std::uint32_t poll_retirements = 0;
};

constexpr void to_json(infra::JsonOut& o, const BayView& v) noexcept {
    o.field("build", v.builds);
    o.field("full", v.bay_full);
    o.field("free", v.frees);
    o.field("wref", v.window_refusals);
    o.field("fedref", v.fed_refusals);
    o.field("padok", v.pad_grants);
    o.field("padref", v.pad_refusals);
    o.field("padlw", v.pads_on_lw);
    o.field("cdok", v.sector_grants);
    o.field("cdref", v.sector_refusals);
    o.field("cdlw", v.cd.on_lw);
    o.field("cdlease", v.cd.leased);
    o.field("cdsrv", v.cd.served);
    o.field("cdres", v.cd.resends);
    o.field("cdabt", v.cd.corrupted);
    o.field("cdblost", v.cd.begin_lost);
    o.field("cddrop", v.cd.drops);
    o.field("cdspur", v.cd.spurious);
    o.field("cdrevk", v.cd.revoked);
    o.field("cdahead", v.cd.read_ahead);
    o.field("cdlat", v.cd.lat_max_us);
    o.field("cdbref", v.cd.bind_refusals);
    o.field("cdfrej", v.cd.fab_rejected);
    o.field("cdfskip", v.cd.fab_skipped);
    o.field("cdfabt", v.cd.fab_aborted);
    o.field("cdfrst", v.cd.fab_reset_fills);
    o.field("cdhold", v.lease_holds);
    o.field("cdlong", v.lease_long_holds);
    o.field("osdok", v.osd_grants);
    o.field("osdref", v.osd_refusals);
    o.field("osdlw", v.osd.on_lw);
    o.field("osdlease", v.osd.leased);
    o.field("osdfrm", v.osd.frames);
    o.field("osdrows", v.osd.rows);
    o.field("osdrevk", v.osd.revoked);
    o.field("osdspi", v.osd_page_rows_link);
    o.field("osdspist", v.osd_status_rows_link);
    o.field("mcok", v.card_grants);
    o.field("mcref", v.card_refusals);
    o.field("mclw", v.cd.cards_on_lw);
    o.field("mcrd", v.cd.card_reads);
    o.field("mcwr", v.cd.card_writes);
    o.field("mcblank", v.cd.card_blanks);
    o.field("mcnew", v.cd.card_creates);
    o.field("mcdisc", v.cd.card_discards);
    o.field("mcfail", v.cd.card_fails);
    o.field("mclat", v.cd.card_lat_max_us);
    o.field("lwbref", v.bridge_refusals);
    o.field("sysok", v.sys_grants);
    o.field("sysref", v.sys_refusals);
    o.field("syspin", v.sys_pin_refusals);
    o.field("syslw", v.sys.on_lw);
    o.field("syslease", v.sys.leased);
    o.field("sysdrain", v.sys.drains);
    o.field("syssnap", v.sys.snapshots);
    o.field("sysretry", v.sys.retries);
    o.field("systorn", v.sys.torn);
    o.field("sysspur", v.sys.spurious);
    o.field("sysrevk", v.sys.revoked);
    o.field("sysbref", v.sys.bind_refusals);
    o.field("syslat", v.sys.lat_max_us);
    o.field("stlw", v.status_lw);
    o.field("stad", v.status_lw_adopts);
    o.field("ststale", v.status_lw_stale);
    o.field("blkret", v.poll_retirements);
}

struct BayRecord {
    static constexpr infra::JsonName kKey{"bay"};
    using View = BayView;
    const app::BayCountsCell& counts;
    infra::OptRef<const app::SectorRouteCell> route{};
    infra::OptRef<const app::OsdRouteCell> osd_route{};
    infra::OptRef<const app::SysRouteCell> sys_route{};

    [[nodiscard]] std::optional<View> sample(const RecordCtx& ctx) const noexcept {
        const app::BayCounts c = counts.sample().value;
        const app::SectorRoute cd = route ? route->sample().value : app::SectorRoute{};
        const app::OsdRoute osd = osd_route ? osd_route->sample().value : app::OsdRoute{};
        const app::SysRoute sys = sys_route ? sys_route->sample().value : app::SysRoute{};
        return View{
            .builds = c.builds,
            .bay_full = c.bay_full,
            .frees = c.frees,
            .window_refusals = c.window_refusals,
            .fed_refusals = ctx.dc.fed_refusals,
            .pad_grants = c.pad_grants,
            .pad_refusals = c.pad_refusals,
            .pads_on_lw = ctx.dc.pads_on_lw,
            .sector_grants = c.sector_grants,
            .sector_refusals = c.sector_refusals,
            .lease_holds = c.lease_holds,
            .lease_long_holds = c.lease_long_holds,
            .cd = cd,
            .osd_grants = c.osd_grants,
            .osd_refusals = c.osd_refusals,
            .osd = osd,
            .osd_page_rows_link = ctx.dc.osd_page_rows_link,
            .osd_status_rows_link = ctx.dc.osd_status_rows_link,
            .card_grants = c.card_grants,
            .card_refusals = c.card_refusals,
            .bridge_refusals = c.bridge_refusals,
            .sys_grants = c.sys_grants,
            .sys_refusals = c.sys_refusals,
            .sys_pin_refusals = c.sys_pin_refusals,
            .sys = sys,
            .status_lw = ctx.dc.status_lw,
            .status_lw_adopts = ctx.dc.status_lw_adopts,
            .status_lw_stale = ctx.dc.status_lw_stale,
            .poll_retirements = c.poll_retirements
        };
    }
};

}
