// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/core_builder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <iterator>
#include <string_view>
#include <utility>

#include "app/core_window_map.h"
#include "app/dev_mem_window_map.h"
#include "app/pcm_ring_feeder.h"
#include "app/sector_route.h"
#include "cores/core_grant.h"
#include "cores/core_profile.h"
#include "cores/generic_core.h"
#include "cores/generic_profile.h"
#include "cores/host_services.h"
#include "cores/registry.h"
#include "hal/fabric_region.h"
#include "hal/fpga_memory.h"
#include "hal/lw_sys_window.h"
#include "hal/osd_block.h"
#include "hal/sector_block.h"
#include "hal/irq_pin.h"
#include "hal/sys_block.h"
#include "hal/sys_directory.h"
#include "infra/lease_order.h"
#include "proto/axi_block_decl.h"
#include "proto/lane_block_decl.h"
#include "proto/osd_block_decl.h"
#include "proto/sector_block_decl.h"
#include "proto/sys_block_decl.h"
#include "reactor/cause_set.h"

namespace mister::app {

void CoreDeleter::operator()(cores::Core* p) const noexcept {
    delete p;
}

CoreBuilder CoreBuilder::on_board(Wiring wiring) noexcept {
    return CoreBuilder(wiring, infra::OptRef<hal::IDoorbellSource>{});
}

CoreBuilder CoreBuilder::over_source(Wiring wiring, hal::IDoorbellSource& src) noexcept {
    return CoreBuilder(wiring, infra::OptRef<hal::IDoorbellSource>{src});
}

hal::IDoorbellSource& CoreBuilder::source_() noexcept {
    if (seam_)
        return *seam_;
    uio_doorbells_.set_lw_window(terms_.lw_window);
    uio_doorbells_.set_line_space(terms_.doorbell_nodes);
    return uio_doorbells_;
}

CoreBuilder::CoreBuilder(Wiring wiring, infra::OptRef<hal::IDoorbellSource> seam) noexcept
    : bay_(wiring.bay),
      releases_(wiring.releases),
      terms_(wiring.terms),
      seam_(seam),
      sectors_(wiring.sectors),
      osds_(wiring.osds),
      sys_cells_(wiring.sys) {}

CoreBuilder::~CoreBuilder() {

    for (auto& lease : held_)
        (void)lease.release();
}

proto::LinkOp::AdoptCore CoreBuilder::build(const Ask& ask) {
    using Built = proto::LinkOp::Built;

    struct ClearReads {
        DirReads& reads;

        ~ClearReads() { reads = {}; }
    } const clear_reads{dir_reads_};

    lane_.reset();

    reap();
    const bool claimed = std::exchange(claimed_, false);
    if (ask.name.empty()) {
        drop_unbuilt_sector_();
        return {.outcome = Built::NoName};
    }
    const cores::LoadHint hint = ask.manifest.path.empty() ? cores::LoadHint::None
                                                           : cores::LoadHint::XmlManifest;
    const auto row = cores::find_core(ask.name, hint);

    if (!terms_.vfs) {
        drop_unbuilt_sector_();
        return {.outcome = row ? Built::NoStorage : Built::RegistryMiss};
    }
    CoreBay::Lease lease = bay_.acquire();
    if (!lease) {
        drop_unbuilt_sector_();
        ++counts_.bay_full;
        publish_counts_();
        return {.outcome = Built::Refused, .err = Errc::would_block};
    }
    BuiltCore& slot = *lease;

    const cores::CoreProfile& profile = cores::profile_for(ask.name, hint);

    if (!claimed && ask.fabric)
        grant_sector_(ask.irqs, ask.lw, profile, ask.fabric_gen);

    const RtRows rt = rt_rows(ask.irqs, ask.lw, unbuilt_cause_());
    slot.lines = open_doorbells(source_(), terms_.doorbells, rt.table());
    slot.pads = grant_pads_(ask.lw);
    const cores::HostServices services{
        *terms_.vfs,
        terms_.link,
        terms_.clock,
        terms_.prefetch,
        infra::OptRef<proto::IImageSink>{terms_.bulk},
        adopt_windows(slot, grant_windows(profile)),
        terms_.discs ? terms_.discs : infra::OptRef<svc::DiscReadService>{terms_.unbound_discs},
        infra::OptRef<proto::SpiFioQueue>{terms_.fio_queue},
        terms_.mailbox,
        terms_.save_copy,
        terms_.disk,
    };
    const cores::CoreGrant grant{services, ask.manifest};
    slot.core = row ? row->make(profile, grant) : cores::make_generic(profile, grant);
    const proto::CoreBaySlot at = xthread::index_as<proto::CoreBaySlot>(*bay_.index_of(&slot));

    do
        ++gen_;
    while (gen_ == 0 || gen_ == infra::at_index(levels_.released_gen, at));
    slot.gen = gen_;
    ++counts_.builds;

    if (sector_ && sector_->gen == 0 && slot.core) {
        sector_->gen = gen_;
    } else {
        drop_unbuilt_sector_();
    }
    if (slot.core)
        grant_osd_(ask.lw, profile, ask.fabric_gen, gen_);
    if (slot.core)
        grant_sys_(ask.irqs, ask.lw, ask.fabric_gen, gen_);
    lane_ = slot.core ? find_lane_(ask.lw) : std::nullopt;

    slot.osd_pages = slot.core && osd_ && osd_->gen == gen_ ? OsdPageRoute::Lw : OsdPageRoute::Link;

    slot.lw_slots = slot.core && sector_ && sector_->gen == gen_ ? sector_->bridge_slots()
                                                                 : proto::BlockSlotMask{};

    slot.status_route = slot.core && sys_ && sys_->gen == gen_ ? StatusRoute::Lw
                                                               : StatusRoute::Link;

    slot.block_poll = block_poll_route(
        ask.mount_slots.united(profile_slot_mask(profile)),
        slot.lw_slots
    );
    if (slot.block_poll == BlockPollRoute::Retired)
        ++counts_.poll_retirements;
    publish_counts_();
    held_[at] = std::move(lease);
    return {.slot = at, .outcome = Built::Core, .gen = gen_};
}

void CoreBuilder::forget(const proto::LinkOp::AdoptCore& op) noexcept {
    const proto::CoreBaySlot at = op.slot;
    if (op.outcome != proto::LinkOp::Built::Core || !in_bay(at))
        return;
    if (!held_[at] || held_[at]->gen != op.gen)
        return;

    if (held_[at]->block_poll == BlockPollRoute::Retired)
        --counts_.poll_retirements;
    free_(at);
}

void CoreBuilder::reap() noexcept {
    BayReleases levels{};
    if (!released_.take_if_changed(releases_, levels))
        return;
    levels_ = levels;
    for (const proto::CoreBaySlot s : kAllBaySlots) {
        if (held_[s] && held_[s]->gen == infra::at_index(levels.released_gen, s))
            free_(s);
    }
}

std::optional<AllLeasesHome> CoreBuilder::leases_home(const ParkReceipt& receipt) noexcept {
    if (sector_ || osd_ || sys_)
        return std::nullopt;
    drop_windows(receipt);
    return AllLeasesHome{};
}

std::optional<AllLeasesHome> CoreBuilder::nothing_lent() const noexcept {
    if (sector_ || osd_ || sys_ || counts_.builds != 0)
        return std::nullopt;
    return AllLeasesHome{};
}

void CoreBuilder::drop_windows(const ParkReceipt&) noexcept {
    for (CoreBay::Lease& l : held_) {
        if (!l)
            continue;
        for (auto& w : l->windows)
            w.reset();
        l->fed_ring.reset();

        l->lines = DoorbellLines{};
        l->pads.reset();
    }
    dir_reads_ = {};
    lane_.reset();
}

Ex<CoreBuilder::Directory> CoreBuilder::directory_(const proto::AxiLwDecl& decl) noexcept {
    DirReads::Entry* free_slot = nullptr;
    for (DirReads::Entry& e : dir_reads_.rows) {
        if (e.read && e.decl == decl)
            return *e.read;
        if (!e.read && !free_slot)
            free_slot = &e;
    }
    auto got = read_directory(terms_.lw_window, terms_.lw_image, decl);
    if (got)
        ++directory_reads_;
    if (free_slot)
        *free_slot = DirReads::Entry{.decl = decl, .read = got};
    return got;
}

std::optional<proto::LaneBlockDecl> CoreBuilder::find_lane_(
    std::span<const proto::AxiLwDecl> lw
) noexcept {
    for (const proto::AxiLwDecl& decl : lw) {
        if (!may_carry_pads(decl))
            continue;
        auto dir = directory_(decl);
        if (!dir)
            continue;
        if (auto lane = proto::find_lane_block(dir->image, decl.size))
            return *lane;
    }
    return std::nullopt;
}

std::optional<LwPads> CoreBuilder::grant_pads_(std::span<const proto::AxiLwDecl> lw) noexcept {
    bool tried = false;
    for (const proto::AxiLwDecl& decl : lw) {
        if (!may_carry_pads(decl))
            continue;
        tried = true;
        auto dir = directory_(decl);
        if (!dir)
            continue;
        auto pads = grant_pads(*dir, decl);
        if (!pads)
            continue;
        ++counts_.pad_grants;
        return std::move(*pads);
    }
    if (tried)
        ++counts_.pad_refusals;
    return std::nullopt;
}

namespace {

[[nodiscard]] Ex<hal::LwSysWindow> discovered(
    hal::PhysRegion lw,
    std::span<std::uint32_t> image,
    const proto::AxiLwDecl& decl
) noexcept {
    if (image.empty())
        return hal::LwSysWindow::discover(lw, decl.offset, decl.size);
    return hal::LwSysWindow::discover_in(image, decl.offset, decl.size);
}

}

Ex<CoreBuilder::Directory> CoreBuilder::read_directory(
    hal::PhysRegion lw_window,
    std::span<std::uint32_t> lw_image,
    const proto::AxiLwDecl& decl
) noexcept {
    const std::size_t len = lw_image.empty() ? lw_window.len : lw_image.size_bytes();
    if (!may_carry_pads(decl) || !decl.offset.fits(len, decl.size)) {
        return std::unexpected(Error{Errc::aperture_range, ERR_SITE(), decl.offset.v});
    }
    auto win = discovered(lw_window, lw_image, decl);
    if (!win)
        return std::unexpected(win.error());
    auto dir = win->map<hal::SysDirectory>(0);
    if (!dir)
        return std::unexpected(dir.error());
    return Directory{*win, proto::SysDirectoryImage::read(*dir)};
}

Ex<LwPads> CoreBuilder::grant_pads(const Directory& dir, const proto::AxiLwDecl& decl) noexcept {
    auto block = proto::find_pad_block(dir.image, decl.size);
    if (!block)
        return std::unexpected(block.error());
    auto regs = dir.win.map<hal::UioRegFile>(block->offset);
    if (!regs)
        return std::unexpected(regs.error());
    return LwPads{.regs = std::move(*regs), .block = *block};
}

bool CoreBuilder::fabric_owned(
    const proto::IrqBinding& b,
    std::span<const proto::AxiLwDecl> lw
) noexcept {
    if (b.klass != reactor::Cause::Blk || b.cause_base == hal::LwOffset{})
        return false;
    return std::ranges::any_of(lw, [&b](const proto::AxiLwDecl& d) noexcept {
        return may_carry_pads(d) && d.holds(b.cause_base);
    });
}

CoreBuilder::RtRows CoreBuilder::rt_rows(
    std::span<const proto::IrqBinding> irqs,
    std::span<const proto::AxiLwDecl> lw,
    std::optional<hal::LwOffset> owned
) noexcept {
    RtRows rt{};
    for (const proto::IrqBinding& b : irqs) {
        if (owned && b.cause_base == *owned && fabric_owned(b, lw))
            continue;
        const std::size_t row = rt.count.size();
        if (row < rt.rows.size()) {
            rt.rows[row] = b;
            rt.count = rt.count.next();
        }
        rt.declared = rt.declared.next();
    }
    return rt;
}

std::optional<hal::LwOffset> CoreBuilder::claim_sector(const Claim& c) noexcept {
    dir_reads_ = {};
    const cores::CoreProfile& profile = cores::profile_for(
        c.name,
        c.manifest ? cores::LoadHint::XmlManifest : cores::LoadHint::None
    );
    drop_unbuilt_sector_();
    grant_sector_(c.irqs, c.lw, profile, c.fabric_gen);
    claimed_ = true;
    publish_counts_();
    return unbuilt_cause_();
}

std::optional<hal::LwOffset> CoreBuilder::unbuilt_cause_() const noexcept {
    if (!sector_ || sector_->gen != 0 || sector_->fabric_lost)
        return std::nullopt;

    return hal::LwOffset{sector_->block.cause};
}

void CoreBuilder::drop_unbuilt_sector_() noexcept {
    if (sector_ && sector_->gen == 0)
        (void)retire_sector(!sector_->fabric_lost);
}

void CoreBuilder::grant_sector_(
    std::span<const proto::IrqBinding> irqs,
    std::span<const proto::AxiLwDecl> lw,
    const cores::CoreProfile& profile,
    FabricGeneration fabric_gen
) noexcept {
    const infra::OptRef<const svc::ISectorProgram> program = profile.sector_program;
    if (!program || !sectors_)
        return;
    if (!std::ranges::any_of(lw, may_carry_pads))
        return;

    if (!retire_sector(sector_ && sector_->fabric_gen == fabric_gen)) {
        ++counts_.sector_refusals;
        return;
    }
    for (const proto::AxiLwDecl& decl : lw) {
        if (!may_carry_pads(decl))
            continue;
        auto dir = directory_(decl);
        if (!dir)
            continue;
        auto got = grant_sector(*dir, decl, irqs, *program, source_(), terms_.doorbells);
        if (!got)
            continue;
        sector_.emplace(std::move(*got));
        sector_->gen = 0;
        sector_->fabric_gen = fabric_gen;
        sector_->blank = infra::OptRef<const cores::BlankSaveSpec>{profile.blank_save};
        sector_->cards = grant_cards_(sector_->block, profile);
        ++counts_.sector_grants;
        return;
    }
    ++counts_.sector_refusals;
}

bool CoreBuilder::card_admitted(
    const cores::CoreProfile& profile,
    const proto::SectorBlockDecl::Card& card,
    proto::SlotIndex block_slot
) noexcept {

    if (profile.saves_by_channel)
        return false;

    const bool save_slot = std::ranges::any_of(profile.slots, [&](const auto& s) noexcept {
        return s.index.block_slot() == card.slot && s.writable && s.role == cores::SlotRole::Save;
    });
    return save_slot
        && card.reads
        && card.writes
        && card.frame_admitted()
        && card.slot.below<hal::SectorBlock::kCardSlots>()
        && card.slot != block_slot;
}

std::array<LwSector::Card, proto::SectorBlockDecl::kCards> CoreBuilder::grant_cards_(
    const proto::SectorBlockDecl& block,
    const cores::CoreProfile& profile
) noexcept {
    std::array<LwSector::Card, proto::SectorBlockDecl::kCards> cards{};
    for (std::size_t c = 0; c < cards.size(); ++c) {
        const proto::SectorBlockDecl::Card& card = block.cards[c];
        if (!card.declared())
            continue;
        if (!card_admitted(profile, card, block.slot)) {
            ++counts_.card_refusals;
            continue;
        }
        auto image = card_region_(card);
        if (!image) {
            ++counts_.card_refusals;
            continue;
        }
        cards[c] = LwSector::Card{.decl = card, .image = *image};
        ++counts_.card_grants;
    }
    return cards;
}

Ex<hal::PhysRegion> CoreBuilder::card_region_(
    const proto::SectorBlockDecl::Card& card
) const noexcept {
    auto region = hal::resolve_load_region(
        terms_.aperture.region,
        os::PhysAddr{card.wsrc},
        card.extent
    );
    if (!region)
        return std::unexpected(region.error());
    region->name = "lw-card-image";
    return *region;
}

Ex<LwSector> CoreBuilder::grant_sector(
    const Directory& dir,
    const proto::AxiLwDecl& decl,
    std::span<const proto::IrqBinding> irqs,
    const svc::ISectorProgram& program,
    hal::IDoorbellSource& src,
    const hal::DoorbellPolicy& policy
) noexcept {
    auto block = proto::find_sector_block(dir.image, decl.size);
    if (!block)
        return std::unexpected(block.error());

    if (block->slot != program.slot() || block->frame_bytes() != program.frame_bytes()) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), block->frame_words});
    }

    const hal::LwOffset at = decl.offset.plus(block->offset);
    const proto::IrqBinding* row = nullptr;
    for (const proto::IrqBinding& b : irqs) {
        if (b.cause_base == at && fabric_owned(b, std::span<const proto::AxiLwDecl>(&decl, 1))) {
            row = &b;
            break;
        }
    }
    if (block->cause != at.v || row == nullptr) {
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), block->cause});
    }

    if (!policy.line_in_pool(row->line)) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), os::to_ordinal(row->line)});
    }
    auto regs = dir.win.map<hal::SectorBlock>(block->offset);
    if (!regs)
        return std::unexpected(regs.error());

    const std::uint32_t want = block->minor == 0 ? hal::SectorBlock::kId : hal::SectorBlock::kIdV2;
    if (const std::uint32_t id = regs->read(hal::SectorBlock::Reg::Id); id != want) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), id});
    }
    auto line = src.open_line(row->line);
    if (!line)
        return std::unexpected(line.error());
    return LwSector{
        .regs = std::move(*regs),
        .line = std::move(*line),
        .block = *block,
        .program = infra::OptRef<const svc::ISectorProgram>{program}
    };
}

void CoreBuilder::raise_sector() noexcept {
    if (!sectors_)
        return;

    lent_gen_ = 0;
    if (!sector_ || sector_->gen != gen_ || sector_->fabric_lost) {

        if (!sector_revoked_)
            sectors_->grant.publish(SectorGrant{});
        sector_revoked_ = true;
        return;
    }
    SectorGrant g{
        .block = sector_->regs.lend(),
        .line = sector_->line.borrow(),
        .program = sector_->program,
        .gen = sector_->gen,
        .frame_words = sector_->block.frame_words,
        .blank = sector_->blank
    };
    for (std::size_t c = 0; c < g.cards.size(); ++c) {
        const LwSector::Card& card = sector_->cards[c];
        if (card.image.len == 0)
            continue;
        g.cards[c] = SectorCard{
            .slot = card.decl.slot.v,
            .frame_words = card.decl.frame_words,
            .image = card.image
        };
    }
    sectors_->grant.publish(g);

    if (sectors_->wake)
        sectors_->wake->request();
    sector_revoked_ = false;
    lent_gen_ = g.gen;
    lent_cards_ = sector_->card_slots();
}

bool CoreBuilder::bridge_refused() noexcept {
    bool refused = false;
    if (lent_gen_ != 0 && judged_gen_ != lent_gen_ && sectors_) {
        const auto s = sectors_->route.sample();
        if (s && s.value.bind_gen == lent_gen_) {
            judged_gen_ = lent_gen_;
            refused = !s.value.on_lw || !lent_cards_.minus(s.value.cards_on_lw).empty();
        }
    }

    if (lent_sys_gen_ != 0 && judged_sys_gen_ != lent_sys_gen_ && sys_cells_) {
        const auto s = sys_cells_->route.sample();
        if (s && s.value.bind_gen == lent_sys_gen_) {
            judged_sys_gen_ = lent_sys_gen_;
            refused = refused || !s.value.on_lw;
        }
    }
    if (!refused)
        return false;
    ++counts_.bridge_refusals;
    publish_counts_();
    return true;
}

bool CoreBuilder::retire_sector(bool fabric_ok) noexcept {
    if (!sector_)
        return true;
    revoke_sector_();
    if (sector_leased_())
        return false;

    if (fabric_ok && !sector_->fabric_lost) {
        sector_->regs.write(hal::SectorBlock::Reg::Ctrl, 0);
        (void)sector_->regs.read(hal::SectorBlock::Reg::Ctrl);
    }
    sector_.reset();
    return true;
}

void CoreBuilder::abandon_sector() noexcept {
    if (!sector_)
        return;
    revoke_sector_();
    sector_->fabric_lost = true;
}

void CoreBuilder::revoke_sector_() noexcept {
    lent_gen_ = 0;
    if (sector_revoked_ || !sectors_)
        return;
    sectors_->grant.publish(SectorGrant{});
    sector_revoked_ = true;
}

proto::BlockSlotMask CoreBuilder::profile_slot_mask(const cores::CoreProfile& profile) noexcept {
    proto::BlockSlotMask mask{};
    for (const cores::FileSlot& s : profile.slots)
        mask = mask.with_or_all(s.index.block_slot());
    return mask;
}

bool CoreBuilder::fabric_route(std::string_view name, bool manifest) noexcept {
    const cores::LoadHint hint = manifest ? cores::LoadHint::XmlManifest : cores::LoadHint::None;
    return cores::profile_for(name, hint).sector_program.has_value();
}

bool CoreBuilder::sector_leased_() const noexcept {
    if (!sectors_)
        return false;
    return xthread::held_after_revoke(sectors_->route, [](const SectorRoute& r) noexcept {
        return r.leased;
    });
}

void CoreBuilder::grant_osd_(
    std::span<const proto::AxiLwDecl> lw,
    const cores::CoreProfile& profile,
    FabricGeneration fabric_gen,
    std::uint16_t gen
) noexcept {
    if (!osds_)
        return;
    if (!std::ranges::any_of(lw, may_carry_pads))
        return;

    if (!retire_osd(osd_ && osd_->fabric_gen == fabric_gen)) {
        ++counts_.osd_refusals;
        return;
    }

    if (profile.is_front_end) {
        ++counts_.osd_refusals;
        return;
    }
    for (const proto::AxiLwDecl& decl : lw) {
        if (!may_carry_pads(decl))
            continue;
        auto dir = directory_(decl);
        if (!dir)
            continue;
        auto got = grant_osd(*dir, decl);
        if (!got)
            continue;
        osd_.emplace(std::move(*got));
        osd_->gen = gen;
        osd_->fabric_gen = fabric_gen;
        ++counts_.osd_grants;
        return;
    }
    ++counts_.osd_refusals;
}

Ex<LwOsd> CoreBuilder::grant_osd(const Directory& dir, const proto::AxiLwDecl& decl) noexcept {
    auto block = proto::find_osd_block(dir.image, decl.size);
    if (!block)
        return std::unexpected(block.error());
    auto regs = dir.win.map<hal::OsdBlock>(block->offset);
    if (!regs)
        return std::unexpected(regs.error());
    if (const std::uint32_t id = regs->read(hal::OsdBlock::Reg::Id); id != hal::OsdBlock::kId) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), id});
    }
    return LwOsd{.regs = std::move(*regs), .block = *block};
}

void CoreBuilder::raise_osd() noexcept {
    if (!osds_)
        return;

    if (!osd_ || osd_->gen != gen_ || osd_->fabric_lost) {
        if (!osd_revoked_)
            osds_->grant.publish(OsdGrant{});
        osd_revoked_ = true;
        return;
    }
    osds_->grant.publish(OsdGrant{.block = osd_->regs.lend(), .gen = osd_->gen});
    osd_revoked_ = false;
}

bool CoreBuilder::retire_osd(bool fabric_ok) noexcept {
    if (!osd_)
        return true;
    revoke_osd_();
    if (osd_leased_())
        return false;

    if (fabric_ok && !osd_->fabric_lost) {
        osd_->regs.write(hal::OsdBlock::Reg::Ctrl, 0);
        (void)osd_->regs.read(hal::OsdBlock::Reg::Ctrl);
    }
    osd_.reset();
    return true;
}

void CoreBuilder::abandon_osd() noexcept {
    if (!osd_)
        return;
    revoke_osd_();
    osd_->fabric_lost = true;
}

void CoreBuilder::revoke_osd_() noexcept {
    if (osd_revoked_ || !osds_)
        return;
    osds_->grant.publish(OsdGrant{});
    osd_revoked_ = true;
}

bool CoreBuilder::osd_leased_() const noexcept {
    if (!osds_)
        return false;
    return xthread::held_after_revoke(osds_->route, [](const OsdRoute& r) noexcept {
        return r.leased;
    });
}

void CoreBuilder::grant_sys_(
    std::span<const proto::IrqBinding> irqs,
    std::span<const proto::AxiLwDecl> lw,
    FabricGeneration fabric_gen,
    std::uint16_t gen
) noexcept {
    if (!sys_cells_)
        return;
    if (!std::ranges::any_of(lw, may_carry_pads))
        return;

    if (!retire_sys()) {
        ++counts_.sys_refusals;
        return;
    }
    for (const proto::AxiLwDecl& decl : lw) {
        if (!may_carry_pads(decl))
            continue;
        auto dir = directory_(decl);
        if (!dir)
            continue;
        auto got = grant_sys(*dir, decl, source_(), terms_.doorbells, irqs);
        if (!got)
            continue;
        sys_.emplace(std::move(*got));
        sys_->gen = gen;
        sys_->fabric_gen = fabric_gen;
        pin_sys_line_(sys_->block.line);
        ++counts_.sys_grants;
        return;
    }
    ++counts_.sys_refusals;
}

Ex<LwSys> CoreBuilder::grant_sys(
    const Directory& dir,
    const proto::AxiLwDecl& decl,
    hal::IDoorbellSource& src,
    const hal::DoorbellPolicy& policy,
    std::span<const proto::IrqBinding> taken
) noexcept {
    auto block = proto::find_sys_block(dir.image, decl.size);
    if (!block)
        return std::unexpected(block.error());

    const hal::LwOffset at = decl.offset.plus(block->offset);
    if (block->cause != at.v) {
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), block->cause});
    }
    if (!policy.line_in_pool(block->line)) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), os::to_ordinal(block->line)});
    }

    if (std::ranges::any_of(taken, [&](const proto::IrqBinding& b) {
            return b.line == block->line;
        })) {
        return std::unexpected(
            Error{Errc::busy, ERR_SITE(), static_cast<std::uint32_t>(block->line)}
        );
    }
    auto regs = dir.win.map<hal::SysBlock>(block->offset);
    if (!regs)
        return std::unexpected(regs.error());
    if (const std::uint32_t id = regs->read(hal::SysBlock::Reg::Id); id != hal::SysBlock::kId) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), id});
    }
    auto line = src.open_line(block->line);
    if (!line)
        return std::unexpected(line.error());
    return LwSys{.regs = std::move(*regs), .line = std::move(*line), .block = *block};
}

void CoreBuilder::pin_sys_line_(os::UioLine line) noexcept {
    if (terms_.doorbell_cpus.empty() || terms_.doorbell_nodes.node_prefix.empty())
        return;
    std::array<char, kNodeNameCap> name{};

    const auto written = std::format_to_n(
        name.data(),
        std::ssize(name),
        "{}{}",
        terms_.doorbell_nodes.node_prefix,
        os::to_ordinal(line) + proto::kIrqPoolFirstLine
    );
    if (written.size >= std::ssize(name)) {
        ++counts_.sys_pin_refusals;
        return;
    }
    const hal::IrqPin pin{
        .action = std::string_view(name.data(), static_cast<std::size_t>(written.size)),
        .cpus = terms_.doorbell_cpus
    };
    if (hal::pin_irq_affinity(pin, terms_.proc_root).outcome != hal::IrqPinOutcome::Applied) {
        ++counts_.sys_pin_refusals;
    }
}

void CoreBuilder::raise_sys() noexcept {
    if (!sys_cells_)
        return;
    lent_sys_gen_ = 0;

    if (!sys_ || sys_->gen != gen_ || sys_->fabric_lost) {
        if (!sys_revoked_)
            sys_cells_->grant.publish(SysGrant{});
        sys_revoked_ = true;
        return;
    }
    sys_cells_->grant.publish(
        SysGrant{.block = sys_->regs.lend(), .line = sys_->line.borrow(), .gen = sys_->gen}
    );
    if (sys_cells_->wake)
        sys_cells_->wake->request();
    sys_revoked_ = false;
    lent_sys_gen_ = sys_->gen;
}

bool CoreBuilder::retire_sys() noexcept {
    if (!sys_)
        return true;
    revoke_sys_();
    if (sys_leased_())
        return false;
    sys_.reset();
    return true;
}

void CoreBuilder::abandon_sys() noexcept {
    if (!sys_)
        return;
    revoke_sys_();
    sys_->fabric_lost = true;
}

void CoreBuilder::revoke_sys_() noexcept {
    lent_sys_gen_ = 0;
    if (sys_revoked_ || !sys_cells_)
        return;
    sys_cells_->grant.publish(SysGrant{});
    sys_revoked_ = true;
}

bool CoreBuilder::sys_leased_() const noexcept {
    if (!sys_cells_)
        return false;
    return xthread::held_after_revoke(sys_cells_->route, [](const SysRoute& r) noexcept {
        return r.leased;
    });
}

void CoreBuilder::count_lease_hold(bool long_hold) noexcept {
    if (long_hold) {
        ++counts_.lease_long_holds;
    } else {
        ++counts_.lease_holds;
    }
    publish_counts_();
}

DoorbellLines CoreBuilder::open_doorbells(
    hal::IDoorbellSource& src,
    const hal::DoorbellPolicy& policy,
    std::span<const proto::IrqBinding> irqs
) noexcept {
    DoorbellLines lines{};
    const std::size_t n = std::min(irqs.size(), kMaxDoorbellRows);
    lines.count = n;
    for (std::size_t i = 0; i < n; ++i) {
        const proto::IrqBinding& b = irqs[i];
        DoorbellLines::Line& line = lines.rows[i];
        line.binding = b;

        if (!policy.line_in_pool(b.line)) {
            line.gate = Errc::slot_range;
            continue;
        }

        if (b.cause_base != hal::LwOffset{}
            && !policy.cause_in_aperture(b.cause_base, hal::CauseReg::kSize)) {
            line.gate = Errc::bad_format;
            continue;
        }
        auto uio = src.open_line(b.line);
        if (!uio) {
            line.why = uio.error().code;
            continue;
        }
        if (b.cause_base != hal::LwOffset{}) {
            auto w = src.map_cause(b.cause_base);
            if (!w) {
                line.why = w.error().code;
                continue;
            }
            line.cause.emplace(std::move(*w));
        }
        line.handle = std::move(*uio);
    }
    return lines;
}

CoreBuilder::GrantedWindows CoreBuilder::grant_windows(const cores::CoreProfile& profile) noexcept {
    GrantedWindows grant{};

    DevMemWindowMap<seat_owner::Main> board_maps{};
    CoreWindowMap window_map{terms_.aperture, board_maps};

    const std::size_t n = std::min(profile.windows.size(), cores::kMaxCoreWindows);
    for (std::size_t i = 0; i < n; ++i) {
        const cores::CoreWindowDecl& row = profile.windows[i];
        GrantedWindows::Row& out = grant.rows[i];
        out.name = row.region.name;

        if (row.feeds_pcm && (!terms_.feeder || grant.fed_ring.has_value())) {
            ++counts_.window_refusals;
            continue;
        }
        auto mem = window_map.map(row.region);
        if (!mem) {
            ++counts_.window_refusals;
            continue;
        }
        if (row.feeds_pcm) {
            grant.fed_ring.emplace(std::move(*mem));
            out.borrowed = infra::OptRef<cores::ICoreWindow>{*terms_.feeder};
            continue;
        }
        out.mapped.emplace(std::move(*mem));
    }
    return grant;
}

cores::CoreWindowGrant CoreBuilder::adopt_windows(
    BuiltCore& slot,
    GrantedWindows windows
) noexcept {
    cores::CoreWindowGrant grant{};
    for (std::size_t i = 0; i < windows.rows.size(); ++i) {
        GrantedWindows::Row& row = windows.rows[i];
        grant.rows[i].name = row.name;

        if (row.mapped) {
            slot.windows[i].emplace(std::move(*row.mapped));
            grant.rows[i].window = infra::OptRef<cores::ICoreWindow>{*slot.windows[i]};
            continue;
        }
        grant.rows[i].window = row.borrowed;
    }
    slot.fed_ring = std::move(windows.fed_ring);
    return grant;
}

std::uint32_t CoreBuilder::leased() const noexcept {
    return static_cast<std::uint32_t>(
        std::ranges::count_if(held_, [](const CoreBay::Lease& l) noexcept { return l != nullptr; })
    );
}

void CoreBuilder::free_(proto::CoreBaySlot slot) noexcept {
    BuiltCore& b = *held_[slot];
    b.core.reset();
    for (auto& w : b.windows)
        w.reset();
    b.fed_ring.reset();
    b.lines = DoorbellLines{};
    b.pads.reset();
    b.osd_pages = OsdPageRoute::Link;
    b.lw_slots = {};
    b.status_route = StatusRoute::Link;
    b.block_poll = BlockPollRoute::Link;
    b.gen = 0;
    held_[slot].reset();
    ++counts_.frees;
    publish_counts_();
}

void CoreBuilder::publish_counts_() noexcept {
    counts_cell_.publish(counts_);
}

}
