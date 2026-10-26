// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/core_builder.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string_view>
#include <utility>

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

CoreBuilder::CoreBuilder(
    CoreBay& bay,
    const BayReleasesCell& releases,
    const CoreTerms& terms,
    infra::OptRef<hal::IDoorbellSource> src,
    SectorCells sectors,
    OsdCells osds,
    SysCells sys
) noexcept
    : bay_(bay),
      releases_(releases),
      terms_(terms),
      doorbells_(src ? static_cast<hal::IDoorbellSource&>(*src) : uio_doorbells_),
      sectors_(sectors),
      osds_(osds),
      sys_cells_(sys) {}

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
        ++bay_full_;
        publish_counts_();
        return {.outcome = Built::Refused, .err = Errc::would_block};
    }
    BuiltCore& slot = *lease;

    const cores::CoreProfile& profile = row ? *(*row)->profile : cores::kGenericProfile;

    if (!claimed && ask.fabric)
        grant_sector_(ask.irqs, ask.lw, profile, ask.fabric_gen);

    std::array<proto::IrqBinding, kMaxDoorbellRows> rt{};
    const std::size_t n_rt = std::min(rt_rows(ask.irqs, ask.lw, rt, unbuilt_cause_()), rt.size());
    open_doorbells_(slot, std::span<const proto::IrqBinding>(rt.data(), n_rt));
    grant_pads_(slot, ask.lw);
    const cores::HostServices services{
        *terms_.vfs,
        terms_.link,
        terms_.clock,
        terms_.prefetch,
        infra::OptRef<proto::IImageSink>{terms_.bulk},
        grant_windows(slot, profile),
        terms_.discs ? terms_.discs : infra::OptRef<svc::DiscReadService>{terms_.unbound_discs},
        infra::OptRef<proto::SpiFioQueue>{terms_.fio_queue},
        terms_.mailbox,
        terms_.save_copy,
        terms_.disk,
    };
    const cores::CoreGrant grant{services, ask.manifest};
    slot.core = row ? (*row)->make(profile, grant) : cores::make_generic(profile, grant);
    const std::uint8_t at = bay_.index_of(&slot);

    do
        ++gen_;
    while (gen_ == 0 || gen_ == levels_.released_gen[at]);
    slot.gen = gen_;
    ++builds_;

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

    slot.lw_slots = slot.core && sector_ && sector_->gen == gen_ ? sector_->bridge_slots() : 0u;

    slot.status_route = slot.core && sys_ && sys_->gen == gen_ ? StatusRoute::Lw
                                                               : StatusRoute::Link;

    slot.block_poll = block_poll_route(
        static_cast<std::uint16_t>(ask.mount_slots | profile_slot_mask(profile)),
        slot.lw_slots
    );
    if (slot.block_poll == BlockPollRoute::Retired)
        ++poll_retirements_;
    publish_counts_();
    held_[at] = std::move(lease);
    return {.slot = at, .outcome = Built::Core, .gen = gen_};
}

void CoreBuilder::forget(const proto::LinkOp::AdoptCore& op) noexcept {
    if (op.outcome != proto::LinkOp::Built::Core || op.slot >= held_.size())
        return;
    if (!held_[op.slot] || held_[op.slot]->gen != op.gen)
        return;

    if (held_[op.slot]->block_poll == BlockPollRoute::Retired)
        --poll_retirements_;
    free_(op.slot);
}

void CoreBuilder::reap() noexcept {
    BayReleases levels{};
    if (!released_.take_if_changed(releases_, levels))
        return;
    levels_ = levels;
    for (std::size_t i = 0; i < held_.size(); ++i) {
        if (held_[i] && held_[i]->gen == levels.released_gen[i])
            free_(static_cast<std::uint8_t>(i));
    }
}

std::optional<AllLeasesHome> CoreBuilder::leases_home(const ParkReceipt& receipt) noexcept {
    if (sector_ || osd_ || sys_)
        return std::nullopt;
    drop_windows(receipt);
    return AllLeasesHome{};
}

std::optional<AllLeasesHome> CoreBuilder::nothing_lent() const noexcept {
    if (sector_ || osd_ || sys_ || builds_ != 0)
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
        if (e.read && e.offset == decl.offset.v && e.size == decl.size)
            return *e.read;
        if (!e.read && !free_slot)
            free_slot = &e;
    }
    auto got = read_directory(terms_.lw_window, terms_.lw_image, decl);
    if (got)
        ++directory_reads_;
    if (free_slot)
        *free_slot = DirReads::Entry{decl.offset.v, decl.size, got};
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

hal::IDoorbellSource& CoreBuilder::doorbell_source_() noexcept {
    uio_doorbells_.set_lw_window(terms_.lw_window);
    uio_doorbells_.set_line_space(terms_.doorbell_nodes);
    return doorbells_;
}

void CoreBuilder::open_doorbells_(
    BuiltCore& into,
    std::span<const proto::IrqBinding> irqs
) noexcept {
    open_doorbells(doorbell_source_(), terms_.doorbells, irqs, into.lines);
}

void CoreBuilder::grant_pads_(BuiltCore& into, std::span<const proto::AxiLwDecl> lw) noexcept {
    into.pads.reset();
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
        into.pads.emplace(std::move(*pads));
        ++pad_grants_;
        return;
    }
    if (tried)
        ++pad_refusals_;
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
    if (!may_carry_pads(decl) || decl.size > len || decl.offset.v > len - decl.size) {
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

Ex<LwPads> CoreBuilder::grant_pads(
    hal::PhysRegion lw_window,
    std::span<std::uint32_t> lw_image,
    const proto::AxiLwDecl& decl
) noexcept {
    auto dir = read_directory(lw_window, lw_image, decl);
    if (!dir)
        return std::unexpected(dir.error());
    return grant_pads(*dir, decl);
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
    if (b.klass != reactor::Cause::Blk || b.cause_base.v == 0)
        return false;
    return std::ranges::any_of(lw, [&b](const proto::AxiLwDecl& d) noexcept {
        return may_carry_pads(d)
            && b.cause_base.v >= d.offset.v
            && b.cause_base.v - d.offset.v < d.size;
    });
}

std::size_t CoreBuilder::rt_rows(
    std::span<const proto::IrqBinding> irqs,
    std::span<const proto::AxiLwDecl> lw,
    std::span<proto::IrqBinding> out,
    std::optional<std::uint32_t> owned
) noexcept {
    std::size_t n = 0;
    for (const proto::IrqBinding& b : irqs) {
        if (owned && b.cause_base.v == *owned && fabric_owned(b, lw))
            continue;
        if (n < out.size())
            out[n] = b;
        ++n;
    }
    return n;
}

std::optional<std::uint32_t> CoreBuilder::claim_sector(const Claim& c) noexcept {
    dir_reads_ = {};
    const auto row = cores::find_core(
        c.name,
        c.manifest ? cores::LoadHint::XmlManifest : cores::LoadHint::None
    );
    const cores::CoreProfile& profile = row ? *(*row)->profile : cores::kGenericProfile;
    drop_unbuilt_sector_();
    grant_sector_(c.irqs, c.lw, profile, c.fabric_gen);
    claimed_ = true;
    publish_counts_();
    return unbuilt_cause_();
}

std::optional<std::uint32_t> CoreBuilder::unbuilt_cause_() const noexcept {
    if (!sector_ || sector_->gen != 0 || sector_->fabric_lost)
        return std::nullopt;
    return sector_->block.cause;
}

void CoreBuilder::drop_unbuilt_sector_() noexcept {
    if (sector_ && sector_->gen == 0)
        (void)retire_sector(!sector_->fabric_lost);
}

void CoreBuilder::grant_sector_(
    std::span<const proto::IrqBinding> irqs,
    std::span<const proto::AxiLwDecl> lw,
    const cores::CoreProfile& profile,
    std::uint32_t fabric_gen
) noexcept {
    const infra::OptRef<const svc::ISectorProgram> program = profile.sector_program;
    if (!program || !sectors_.grant || !sectors_.route)
        return;
    if (!std::ranges::any_of(lw, may_carry_pads))
        return;

    if (!retire_sector(sector_ && sector_->fabric_gen == fabric_gen)) {
        ++sector_refusals_;
        return;
    }
    for (const proto::AxiLwDecl& decl : lw) {
        if (!may_carry_pads(decl))
            continue;
        auto dir = directory_(decl);
        if (!dir)
            continue;
        auto got = grant_sector(*dir, decl, irqs, *program, doorbell_source_(), terms_.doorbells);
        if (!got)
            continue;
        sector_.emplace(std::move(*got));
        sector_->gen = 0;
        sector_->fabric_gen = fabric_gen;
        grant_cards_(*sector_, profile);
        ++sector_grants_;
        return;
    }
    ++sector_refusals_;
}

bool CoreBuilder::card_admitted(
    const cores::CoreProfile& profile,
    const proto::SectorBlockDecl::Card& card,
    std::uint8_t block_slot
) noexcept {

    if (profile.saves_by_channel)
        return false;
    const std::uint32_t bytes = static_cast<std::uint32_t>(card.frame_words) * 4u;
    const bool save_slot = std::ranges::any_of(profile.slots, [&](const auto& s) noexcept {
        return s.index.v == card.slot && s.writable && s.role == cores::SlotRole::Save;
    });
    const bool both = (card.flags & proto::SectorBlockDecl::Card::kRead) != 0
        && (card.flags & proto::SectorBlockDecl::Card::kWrite) != 0;
    const bool block = bytes >= 128u
        && bytes <= hal::SectorBlock::kMaxFrameWords * 4u
        && (bytes & (bytes - 1u)) == 0u;
    return save_slot
        && both
        && block
        && card.slot < 4u
        && card.slot != block_slot
        && card.extent >= bytes
        && card.extent % bytes == 0u;
}

void CoreBuilder::grant_cards_(LwSector& into, const cores::CoreProfile& profile) noexcept {
    into.blank = infra::OptRef<const cores::BlankSaveSpec>{profile.blank_save};
    for (std::size_t c = 0; c < into.cards.size(); ++c) {
        const proto::SectorBlockDecl::Card& card = into.block.cards[c];
        if (!card.declared())
            continue;
        if (!card_admitted(profile, card, into.block.slot)) {
            ++card_refusals_;
            continue;
        }
        auto image = card_region_(card);
        if (!image) {
            ++card_refusals_;
            continue;
        }
        into.cards[c] = LwSector::Card{.decl = card, .image = *image};
        ++card_grants_;
    }
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
    hal::PhysRegion lw_window,
    std::span<std::uint32_t> lw_image,
    const proto::AxiLwDecl& decl,
    std::span<const proto::IrqBinding> irqs,
    const svc::ISectorProgram& program,
    hal::IDoorbellSource& src,
    const hal::DoorbellPolicy& policy
) noexcept {
    auto dir = read_directory(lw_window, lw_image, decl);
    if (!dir)
        return std::unexpected(dir.error());
    return grant_sector(*dir, decl, irqs, program, src, policy);
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

    if (block->slot != program.slot()
        || static_cast<std::uint32_t>(block->frame_words) * 4u != program.frame_bytes()) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), block->frame_words});
    }

    const std::uint32_t at = decl.offset.v + block->offset;
    const proto::IrqBinding* row = nullptr;
    for (const proto::IrqBinding& b : irqs) {
        if (fabric_owned(b, std::span<const proto::AxiLwDecl>(&decl, 1))
            && b.cause_base.v == block->cause) {
            row = &b;
            break;
        }
    }
    if (block->cause != at || row == nullptr) {
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), block->cause});
    }

    if (!policy.line_in_pool(static_cast<std::uint32_t>(row->line))) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(row->line)}
        );
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
    if (!sectors_.grant)
        return;

    lent_gen_ = 0;
    if (!sector_ || sector_->gen != gen_ || sector_->fabric_lost) {

        if (!sector_revoked_)
            sectors_.grant->publish(SectorGrant{});
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
            .slot = card.decl.slot,
            .frame_words = card.decl.frame_words,
            .image = card.image
        };
    }
    sectors_.grant->publish(g);

    if (sectors_.wake)
        sectors_.wake->request();
    sector_revoked_ = false;
    lent_gen_ = g.gen;
    lent_cards_ = sector_->card_slots();
}

bool CoreBuilder::bridge_refused() noexcept {
    bool refused = false;
    if (lent_gen_ != 0 && judged_gen_ != lent_gen_ && sectors_.route) {
        const auto s = sectors_.route->sample();
        if (s && s.value.bind_gen == lent_gen_) {
            judged_gen_ = lent_gen_;
            refused = s.value.on_lw == 0 || (s.value.cards_on_lw & lent_cards_) != lent_cards_;
        }
    }

    if (lent_sys_gen_ != 0 && judged_sys_gen_ != lent_sys_gen_ && sys_cells_.route) {
        const auto s = sys_cells_.route->sample();
        if (s && s.value.bind_gen == lent_sys_gen_) {
            judged_sys_gen_ = lent_sys_gen_;
            refused = refused || s.value.on_lw == 0;
        }
    }
    if (!refused)
        return false;
    ++bridge_refusals_;
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
    if (sector_revoked_ || !sectors_.grant)
        return;
    sectors_.grant->publish(SectorGrant{});
    sector_revoked_ = true;
}

std::uint16_t CoreBuilder::profile_slot_mask(const cores::CoreProfile& profile) noexcept {
    std::uint16_t mask = 0;
    for (const cores::FileSlot& s : profile.slots) {
        if (s.index.v >= 16u)
            return 0xFFFFu;
        mask = static_cast<std::uint16_t>(mask | (1u << s.index.v));
    }
    return mask;
}

bool CoreBuilder::fabric_route(std::string_view name, bool manifest) noexcept {
    const auto row = cores::find_core(
        name,
        manifest ? cores::LoadHint::XmlManifest : cores::LoadHint::None
    );
    const cores::CoreProfile& profile = row ? *(*row)->profile : cores::kGenericProfile;
    return profile.sector_program.has_value();
}

bool CoreBuilder::sector_leased_() const noexcept {
    if (!sectors_.route)
        return false;
    return xthread::LeaseOrder::held_after_revoke(
        *sectors_.route,
        [](const SectorRoute& r) noexcept { return r.leased != 0; }
    );
}

void CoreBuilder::grant_osd_(
    std::span<const proto::AxiLwDecl> lw,
    const cores::CoreProfile& profile,
    std::uint32_t fabric_gen,
    std::uint16_t gen
) noexcept {
    if (!osds_.grant || !osds_.route)
        return;
    if (!std::ranges::any_of(lw, may_carry_pads))
        return;

    if (!retire_osd(osd_ && osd_->fabric_gen == fabric_gen)) {
        ++osd_refusals_;
        return;
    }

    if (profile.is_front_end) {
        ++osd_refusals_;
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
        ++osd_grants_;
        return;
    }
    ++osd_refusals_;
}

Ex<LwOsd> CoreBuilder::grant_osd(
    hal::PhysRegion lw_window,
    std::span<std::uint32_t> lw_image,
    const proto::AxiLwDecl& decl
) noexcept {
    auto dir = read_directory(lw_window, lw_image, decl);
    if (!dir)
        return std::unexpected(dir.error());
    return grant_osd(*dir, decl);
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
    if (!osds_.grant)
        return;

    if (!osd_ || osd_->gen != gen_ || osd_->fabric_lost) {
        if (!osd_revoked_)
            osds_.grant->publish(OsdGrant{});
        osd_revoked_ = true;
        return;
    }
    osds_.grant->publish(OsdGrant{.block = osd_->regs.lend(), .gen = osd_->gen});
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
    if (osd_revoked_ || !osds_.grant)
        return;
    osds_.grant->publish(OsdGrant{});
    osd_revoked_ = true;
}

bool CoreBuilder::osd_leased_() const noexcept {
    if (!osds_.route)
        return false;
    return xthread::LeaseOrder::held_after_revoke(*osds_.route, [](const OsdRoute& r) noexcept {
        return r.leased != 0;
    });
}

void CoreBuilder::grant_sys_(
    std::span<const proto::IrqBinding> irqs,
    std::span<const proto::AxiLwDecl> lw,
    std::uint32_t fabric_gen,
    std::uint16_t gen
) noexcept {
    if (!sys_cells_.grant || !sys_cells_.route)
        return;
    if (!std::ranges::any_of(lw, may_carry_pads))
        return;

    if (!retire_sys()) {
        ++sys_refusals_;
        return;
    }
    for (const proto::AxiLwDecl& decl : lw) {
        if (!may_carry_pads(decl))
            continue;
        auto dir = directory_(decl);
        if (!dir)
            continue;
        auto got = grant_sys(*dir, decl, doorbell_source_(), terms_.doorbells, irqs);
        if (!got)
            continue;
        sys_.emplace(std::move(*got));
        sys_->gen = gen;
        sys_->fabric_gen = fabric_gen;
        pin_sys_line_(sys_->block.line);
        ++sys_grants_;
        return;
    }
    ++sys_refusals_;
}

Ex<LwSys> CoreBuilder::grant_sys(
    hal::PhysRegion lw_window,
    std::span<std::uint32_t> lw_image,
    const proto::AxiLwDecl& decl,
    hal::IDoorbellSource& src,
    const hal::DoorbellPolicy& policy,
    std::span<const proto::IrqBinding> taken
) noexcept {
    auto dir = read_directory(lw_window, lw_image, decl);
    if (!dir)
        return std::unexpected(dir.error());
    return grant_sys(*dir, decl, src, policy, taken);
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

    const std::uint32_t at = decl.offset.v + block->offset;
    if (block->cause != at) {
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), block->cause});
    }
    if (!policy.line_in_pool(static_cast<std::uint32_t>(block->line))) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(block->line)}
        );
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
    if (terms_.doorbell_cpus.v == 0 || terms_.doorbell_nodes.node_prefix.empty())
        return;
    char name[48];
    const int n = std::snprintf(
        name,
        sizeof name,
        "%.*s%u",
        static_cast<int>(terms_.doorbell_nodes.node_prefix.size()),
        terms_.doorbell_nodes.node_prefix.data(),
        static_cast<unsigned>(line) + 1u
    );
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof name) {
        ++sys_pin_refusals_;
        return;
    }
    const hal::IrqPin pin{
        .action = std::string_view(name, static_cast<std::size_t>(n)),
        .cpus = terms_.doorbell_cpus
    };
    if (hal::pin_irq_affinity(pin, terms_.proc_root).outcome != hal::IrqPinOutcome::Applied) {
        ++sys_pin_refusals_;
    }
}

void CoreBuilder::raise_sys() noexcept {
    if (!sys_cells_.grant)
        return;
    lent_sys_gen_ = 0;

    if (!sys_ || sys_->gen != gen_ || sys_->fabric_lost) {
        if (!sys_revoked_)
            sys_cells_.grant->publish(SysGrant{});
        sys_revoked_ = true;
        return;
    }
    sys_cells_.grant->publish(
        SysGrant{.block = sys_->regs.lend(), .line = sys_->line.borrow(), .gen = sys_->gen}
    );
    if (sys_cells_.wake)
        sys_cells_.wake->request();
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
    if (sys_revoked_ || !sys_cells_.grant)
        return;
    sys_cells_.grant->publish(SysGrant{});
    sys_revoked_ = true;
}

bool CoreBuilder::sys_leased_() const noexcept {
    if (!sys_cells_.route)
        return false;
    return xthread::LeaseOrder::held_after_revoke(
        *sys_cells_.route,
        [](const SysRoute& r) noexcept { return r.leased != 0; }
    );
}

void CoreBuilder::count_lease_hold(bool long_hold) noexcept {
    if (long_hold) {
        ++lease_long_holds_;
    } else {
        ++lease_holds_;
    }
    publish_counts_();
}

void CoreBuilder::open_doorbells(
    hal::IDoorbellSource& src,
    const hal::DoorbellPolicy& policy,
    std::span<const proto::IrqBinding> irqs,
    DoorbellLines& into
) noexcept {
    into = DoorbellLines{};
    const std::size_t n = std::min(irqs.size(), kMaxDoorbellRows);
    into.count = n;
    for (std::size_t i = 0; i < n; ++i) {
        const proto::IrqBinding& b = irqs[i];
        DoorbellLines::Line& line = into.rows[i];
        line.binding = b;

        if (!policy.line_in_pool(static_cast<std::uint32_t>(b.line))) {
            line.gate = Errc::slot_range;
            continue;
        }

        if (b.cause_base.v != 0 && !policy.cause_in_aperture(b.cause_base, hal::CauseReg::kSize)) {
            line.gate = Errc::bad_format;
            continue;
        }
        auto uio = src.open_line(b.line);
        if (!uio) {
            line.why = uio.error().code;
            continue;
        }
        if (b.cause_base.v != 0) {
            auto w = src.map_cause(b.cause_base);
            if (!w) {
                line.why = w.error().code;
                continue;
            }
            line.cause.emplace(std::move(*w));
        }
        line.handle = std::move(*uio);
    }
}

cores::CoreWindowGrant CoreBuilder::grant_windows(
    BuiltCore& into,
    const cores::CoreProfile& profile
) noexcept {
    cores::CoreWindowGrant grant{};

    const std::size_t n = std::min(profile.windows.size(), cores::kMaxCoreWindows);
    for (std::size_t i = 0; i < n; ++i) {
        const cores::CoreWindowDecl& row = profile.windows[i];
        grant.names[i] = row.region.name;

        if (row.feeds_pcm && (!terms_.feeder || into.fed_ring.has_value())) {
            ++window_refusals_;
            continue;
        }
        auto mem = CoreWindow::map_row(terms_.aperture, row.region);
        if (!mem) {
            ++window_refusals_;
            continue;
        }
        if (row.feeds_pcm) {
            into.fed_ring.emplace(std::move(*mem));
            grant.windows[i] = &*terms_.feeder;
            continue;
        }
        into.windows[i].emplace(std::move(*mem));
        grant.windows[i] = &*into.windows[i];
    }
    return grant;
}

std::uint32_t CoreBuilder::leased() const noexcept {
    return static_cast<std::uint32_t>(
        std::ranges::count_if(held_, [](const CoreBay::Lease& l) noexcept { return l != nullptr; })
    );
}

void CoreBuilder::free_(std::uint8_t slot) noexcept {
    BuiltCore& b = *held_[slot];
    b.core.reset();
    for (auto& w : b.windows)
        w.reset();
    b.fed_ring.reset();
    b.lines = DoorbellLines{};
    b.pads.reset();
    b.osd_pages = OsdPageRoute::Link;
    b.lw_slots = 0;
    b.status_route = StatusRoute::Link;
    b.block_poll = BlockPollRoute::Link;
    b.gen = 0;
    held_[slot].reset();
    ++frees_;
    publish_counts_();
}

void CoreBuilder::publish_counts_() noexcept {
    counts_cell_.publish(
        BayCounts{
            .builds = builds_,
            .bay_full = bay_full_,
            .frees = frees_,
            .window_refusals = window_refusals_,
            .pad_grants = pad_grants_,
            .pad_refusals = pad_refusals_,
            .sector_grants = sector_grants_,
            .sector_refusals = sector_refusals_,
            .lease_holds = lease_holds_,
            .lease_long_holds = lease_long_holds_,
            .osd_grants = osd_grants_,
            .osd_refusals = osd_refusals_,
            .card_grants = card_grants_,
            .card_refusals = card_refusals_,
            .bridge_refusals = bridge_refusals_,
            .sys_grants = sys_grants_,
            .sys_refusals = sys_refusals_,
            .sys_pin_refusals = sys_pin_refusals_,
            .poll_retirements = poll_retirements_
        }
    );
}

}
