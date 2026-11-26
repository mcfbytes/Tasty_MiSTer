// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/bay_counts.h"
#include "app/osd_route.h"
#include "app/pad_route.h"
#include "app/sector_grant.h"
#include "app/sector_route.h"
#include "app/sys_route.h"
#include "hal/entry_mask.h"
#include "hal/reset_counts.h"
#include "hal/sys_directory.h"
#include "infra/json_out.h"
#include "infra/opt_ref.h"
#include "infra/tally.h"
#include "record_ctx.h"
#include "svc/io_pass.h"

namespace mister::fw {

struct BayView {
    std::uint32_t builds = 0;
    std::uint32_t bay_full = 0;
    std::uint32_t frees = 0;
    std::uint32_t window_refusals = 0;
    std::uint32_t fed_refusals = 0;
    std::uint32_t pad_grants = 0;
    std::uint32_t pad_refusals = 0;
    infra::Tally pad_no_arm{};
    app::PadRoute pad{};
    infra::CellGen pad_lease{};
    std::uint32_t sector_grants = 0;
    std::uint32_t sector_refusals = 0;
    std::uint32_t lease_holds = 0;
    std::uint32_t lease_long_holds = 0;
    app::SectorRoute cd{};
    infra::CellGen cd_lease{};
    std::uint32_t osd_grants = 0;
    std::uint32_t osd_refusals = 0;
    app::OsdRoute osd{};
    infra::CellGen osd_lease{};
    std::uint32_t osd_page_rows_link = 0;
    std::uint32_t osd_status_rows_link = 0;
    std::uint32_t card_grants = 0;
    std::uint32_t card_refusals = 0;
    std::uint32_t bridge_refusals = 0;
    std::uint32_t sys_grants = 0;
    std::uint32_t sys_refusals = 0;
    std::uint32_t sys_pin_refusals = 0;
    app::SysRoute sys{};
    infra::CellGen sys_lease{};
    std::uint32_t status_off_link = 0;
    std::uint32_t status_snapshot_adopts = 0;
    std::uint32_t status_snapshot_stale = 0;
    std::uint32_t poll_retirements = 0;
    infra::Tally enables{};
    infra::Tally enable_refusals{};
    infra::Tally open_refusals{};
    infra::Tally enable_lost{};
    hal::EntryMask owned_last{};
    hal::ResetCounts resets_last{};
    svc::IoPassMicros io_pass_max{};
    svc::IoPassMicros io_pass_p99{};
    svc::IoPassCount io_pass_bulk_run_max{};
    infra::Tally lane_grants{};
    infra::Tally lane_refusals{};
    infra::Tally lane_busy{};
};

constexpr void to_json(infra::JsonOut& o, const BayView& v) noexcept {
    o.field("build", v.builds);
    o.field("full", v.bay_full);
    o.field("free", v.frees);
    o.field("wref", v.window_refusals);
    o.field("fedref", v.fed_refusals);
    o.field("padok", v.pad_grants);
    o.field("padref", v.pad_refusals);
    o.field("padnoarm", v.pad_no_arm);
    o.field("padlw", v.pad.bound);
    o.field("padlease", v.pad_lease.held());
    o.field("padstores", v.pad.stores);
    o.field("padrevk", v.pad.revoked);
    o.field("padgated", v.pad.gated_stores);
    o.field("cdok", v.sector_grants);
    o.field("cdref", v.sector_refusals);
    o.field("cdlw", v.cd.bound);
    o.field("cdlease", v.cd_lease.held());
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
    o.field("cdoc", v.cd.on_ocram);
    o.field("cdreq", v.cd.requests);
    o.field("cdvoid", v.cd.voided);
    o.field("cdrebind", v.cd.rebinds);
    o.field("cdfull", v.cd.full);
    o.field("cdlost", v.cd.ring_lost);
    o.field("cdfault", v.cd.ring_faults);
    o.field("cdhold", v.lease_holds);
    o.field("cdlong", v.lease_long_holds);
    o.field("osdok", v.osd_grants);
    o.field("osdref", v.osd_refusals);
    o.field("osdlw", v.osd.bound);
    o.field("osdlease", v.osd_lease.held());
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
    o.field("mcmiss", v.cd.card_misses);
    o.field("mclat", v.cd.card_lat_max_us);
    o.field("mcoc", v.cd.mount_frames);
    o.field("mcreq", v.cd.mount_requests);
    o.field("mclost", v.cd.mount_lost);
    o.field("lwbref", v.bridge_refusals);
    o.field("sysok", v.sys_grants);
    o.field("sysref", v.sys_refusals);
    o.field("syspin", v.sys_pin_refusals);
    o.field("syslw", v.sys.bound);
    o.field("syslease", v.sys_lease.held());
    o.field("sysdrain", v.sys.drains);
    o.field("syssnap", v.sys.snapshots);
    o.field("sysretry", v.sys.retries);
    o.field("systorn", v.sys.torn);
    o.field("sysspur", v.sys.spurious);
    o.field("sysrevk", v.sys.revoked);
    o.field("sysbref", v.sys.bind_refusals);
    o.field("syslat", v.sys.lat_max_us);
    o.field("stlw", v.status_off_link);
    o.field("stad", v.status_snapshot_adopts);
    o.field("ststale", v.status_snapshot_stale);
    o.field("blkret", v.poll_retirements);
    o.field("lwen", v.enables);
    o.field("lwenref", v.enable_refusals);
    o.field("lwopref", v.open_refusals);
    o.field("lwenlost", v.enable_lost);
    o.field("lwowned", v.owned_last);
    o.field("lwhrst", v.resets_last.hrst);
    o.field("lwfreed", v.resets_last.freed);
    o.field("iopmax", v.io_pass_max.count());
    o.field("iop99", v.io_pass_p99.count());
    o.field("iobdef", v.io_pass_bulk_run_max);
    o.field("lnok", v.lane_grants);
    o.field("lnref", v.lane_refusals);
    o.field("lnbusy", v.lane_busy);
}

struct BayRecord {
    static constexpr infra::JsonName kKey{"bay"};
    using View = BayView;
    const app::BayCountsCell& counts;
    infra::OptRef<const app::SectorRouteCell> route{};
    infra::OptRef<const app::OsdRouteCell> osd_route{};
    infra::OptRef<const app::SysRouteCell> sys_route{};
    infra::OptRef<const app::SectorLeaseCell> sector_lease{};
    infra::OptRef<const app::OsdLeaseCell> osd_lease{};
    infra::OptRef<const app::SysLeaseCell> sys_lease{};
    infra::OptRef<const svc::IoPassCell> io_pass{};
    infra::OptRef<const app::PadRouteCell> pad_route{};
    infra::OptRef<const app::PadLeaseCell> pad_lease{};

    [[nodiscard]] std::optional<View> sample(const RecordCtx& ctx) const noexcept {
        const app::BayCounts c = counts.sample().value;
        const app::SectorRoute cd = route ? route->sample().value : app::SectorRoute{};
        const app::OsdRoute osd = osd_route ? osd_route->sample().value : app::OsdRoute{};
        const app::SysRoute sys = sys_route ? sys_route->sample().value : app::SysRoute{};
        const infra::CellGen cd_lease = sector_lease ? sector_lease->sample().value
                                                     : infra::CellGen{};
        const infra::CellGen osd_lease_gen = osd_lease ? osd_lease->sample().value
                                                       : infra::CellGen{};
        const infra::CellGen sys_lease_gen = sys_lease ? sys_lease->sample().value
                                                       : infra::CellGen{};
        const svc::IoPass io_pass_seen = io_pass ? io_pass->sample().value : svc::IoPass{};
        const app::PadRoute pad = pad_route ? pad_route->sample().value : app::PadRoute{};
        const infra::CellGen pad_lease_gen = pad_lease ? pad_lease->sample().value
                                                       : infra::CellGen{};
        return View{
            .builds = c.builds,
            .bay_full = c.bay_full,
            .frees = c.frees,
            .window_refusals = c.window_refusals,
            .fed_refusals = ctx.dc.fed_refusals,
            .pad_grants = c.pad_grants,
            .pad_refusals = c.pad_refusals,
            .pad_no_arm = c.pad_no_arm,
            .pad = pad,
            .pad_lease = pad_lease_gen,
            .sector_grants = c.sector_grants,
            .sector_refusals = c.sector_refusals,
            .lease_holds = c.lease_holds,
            .lease_long_holds = c.lease_long_holds,
            .cd = cd,
            .cd_lease = cd_lease,
            .osd_grants = c.osd_grants,
            .osd_refusals = c.osd_refusals,
            .osd = osd,
            .osd_lease = osd_lease_gen,
            .osd_page_rows_link = ctx.dc.osd_page_rows_link,
            .osd_status_rows_link = ctx.dc.osd_status_rows_link,
            .card_grants = c.card_grants,
            .card_refusals = c.card_refusals,
            .bridge_refusals = c.bridge_refusals,
            .sys_grants = c.sys_grants,
            .sys_refusals = c.sys_refusals,
            .sys_pin_refusals = c.sys_pin_refusals,
            .sys = sys,
            .sys_lease = sys_lease_gen,
            .status_off_link = ctx.dc.status_off_link,
            .status_snapshot_adopts = ctx.dc.status_snapshot_adopts,
            .status_snapshot_stale = ctx.dc.status_snapshot_stale,
            .poll_retirements = c.poll_retirements,
            .enables = c.enables,
            .enable_refusals = c.enable_refusals,
            .open_refusals = c.open_refusals,
            .enable_lost = c.enable_lost,
            .owned_last = c.owned_last,
            .resets_last = c.resets_last,
            .io_pass_max = io_pass_seen.max_micros(),
            .io_pass_p99 = io_pass_seen.p99(),
            .io_pass_bulk_run_max = io_pass_seen.bulk_run_max,
            .lane_grants = c.lane_grants,
            .lane_refusals = c.lane_refusals,
            .lane_busy = c.lane_busy
        };
    }
};

}
