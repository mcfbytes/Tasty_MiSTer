// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/core_builder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <format>
#include <iterator>
#include <string_view>
#include <utility>

#include "app/core_window_map.h"
#include "app/dev_mem_window_map.h"
#include "app/pcm/pcm_ring_feeder.h"
#include "app/sector_route.h"
#include "cores/core_grant.h"
#include "cores/core_profile.h"
#include "cores/disk_servant_row.h"
#include "cores/generic_core.h"
#include "cores/generic_profile.h"
#include "cores/host_services.h"
#include "cores/registry.h"
#include "hal/fabric_region.h"
#include "hal/fpga_memory.h"
#include "hal/lw_sys_window.h"
#include "hal/osd_block.h"
#include "hal/pad_ident.h"
#include "hal/sector_block.h"
#include "hal/irq_pin.h"
#include "hal/sys_block.h"
#include "hal/sys_directory.h"
#include "infra/xthread/grant_lease.h"
#include "infra/prelude.h"
#include "os/map_length.h"
#include "proto/link/pad_block_decl.h"
#include "proto/link/lane_block_decl.h"
#include "proto/link/osd_block_decl.h"
#include "proto/link/sector_block_decl.h"
#include "proto/link/sys_block_decl.h"
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
      lends_(
          wiring.sectors,
          wiring.sectors,
          wiring.osds,
          wiring.osds,
          std::move(wiring.sys),
          std::move(wiring.pads)
      ),
      enable_(counts_, counts_cell_, terms_.clock),
      ocram_(
          counts_,
          counts_cell_,
          terms_.clock,
          OcramHostEnable::Wiring{
              .window = wiring.ocram_window,
              .platform = wiring.ocram_platform,
              .mapped = wiring.ocram_mapped,
              .delay = wiring.ocram_delay
          }
      ),
      download_lane_(wiring.download_lane) {}

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

    enable_.clear_masks();

    if (!claimed) {
        enable_.drop();
        enable_header_.reset();
    }
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
    const bool channel_saves = row && row->saves_by_channel();
    if (channel_saves)
        slot.save_store = row->make_save_store(*terms_.vfs, terms_.disk_activity);

    if (channel_saves && slot.save_store == nullptr) {
        drop_unbuilt_sector_();
        return {.outcome = Built::Refused, .err = Errc::contract};
    }

    cores::DiskPorts disk_ports{};
    if (row) {
        const cores::ServantGrant serving{
            .vfs = *terms_.vfs,
            .activity = terms_.disk_activity,
            .remount = terms_.remount
        };
        for (const cores::DiskServantRow& declared : row->disk_servants) {
            std::unique_ptr<cores::IDiskServant>& servant = slot.disk_servants[declared.protocol];
            servant = declared.make(profile, serving);

            if (servant == nullptr) {
                drop_unbuilt_sector_();
                slot.disk_servants = {};
                slot.save_store.reset();
                return {.outcome = Built::Refused, .err = Errc::contract};
            }
            disk_ports[declared.protocol] = terms_.disk[declared.protocol];
        }
    }

    cores::SlotServantGrant lent{};
    if (row) {
        const cores::SlotServantTerms servant_terms{
            .vfs = *terms_.vfs,
            .remount = terms_.remount,
            .mount_reads = terms_.mount_reads
        };
        for (const cores::SlotServantRow& servant : row->slot_servants) {
            auto& held = slot.slot_servants[servant.slot];
            held = servant.make(servant_terms);
            lent.by_slot[servant.slot] = infra::opt_ref_of(held.get());
        }
    }

    if (!claimed && ask.fabric)
        grant_sector_(ask.irqs, ask.lw, profile, ask.fabric_gen, ask.lw_presence);

    const cores::GrantSet grants = profile.grants;
    infra::OptRef<svc::disc::DiscReadService> discs{};
    if (grants.has(cores::Grant::Discs)) {
        discs = terms_.discs ? terms_.discs
                             : infra::OptRef<svc::disc::DiscReadService>{terms_.unbound_discs};
    }
    const cores::HostServices services{
        .link = terms_.link,
        .clock = terms_.clock,
        .bulk = terms_.bulk,
        .fio_queue = terms_.fio_queue,
        .prefetch = terms_.prefetch,
        .windows = adopt_windows(slot, grant_windows(profile)),
        .discs = discs,
        .mailbox = grants.has(cores::Grant::Mailbox) ? terms_.mailbox
                                                     : infra::OptRef<cores::IMailboxPort>{},
        .save_copy = grants.has(cores::Grant::SaveCopy)
            ? terms_.save_copy
            : infra::OptRef<const cores::SaveCopyCell>{},
        .disk = disk_ports,
        .lamp = grants.has(cores::Grant::Lamp) ? terms_.lamp : infra::OptRef<hal::DiskLamp>{},
        .disk_activity = terms_.disk_activity,
        .save_store = infra::opt_ref_of(slot.save_store.get()),
        .slot_servants = lent
    };
    const cores::CoreGrant grant{*terms_.vfs, services, ask.manifest};
    slot.core = row ? row->make(profile, grant) : cores::make_generic(profile, grant);
    const proto::CoreBaySlot at = xthread::index_as<proto::CoreBaySlot>(*bay_.index_of(&slot));

    const proto::BuildGeneration released = infra::at_index(levels_.released_gen, at);
    do {
        gen_ = gen_.next();
    } while (gen_ == released);
    slot.gen = gen_;
    ++counts_.builds;

    if (!(slot.core && sector().adopt_claim(LwLendKey{}, gen_)))
        drop_unbuilt_sector_();

    if (slot.core)
        grant_ocram_sector_(ask.irqs, profile, ask.fabric_gen, gen_, ask.conf_gen);
    if (slot.core)
        grant_pad_(ask.lw, profile, ask.terms, ask.fabric_gen, gen_, ask.lw_presence);

    if (slot.core) {
        if (ocram_.has_osd()) {
            grant_ocram_osd_(ask.fabric_gen, gen_);
        } else {
            grant_osd_(ask.lw, profile, ask.fabric_gen, gen_, ask.lw_presence);
        }
    }
    if (slot.core)
        grant_sys_(ask.irqs, ask.lw, ask.fabric_gen, gen_, ask.lw_presence);

    if (slot.core)
        grant_ocram_lane_(ask.irqs);
    lane_ = slot.core ? find_lane_(ask.lw, ask.lw_presence) : std::nullopt;

    slot.routes.osd = slot.core && ocram_osd().built_as(gen_)
        ? Carrier::Ocram
        : (slot.core && osd().built_as(gen_) ? Carrier::Lw : Carrier::Spi);

    slot.routes.pads = slot.core && pad().built_as(gen_) ? Carrier::Lw : Carrier::Spi;

    slot.routes.off_link_slots = slot.core && sector().built_as(gen_)
        ? sector().held()->bridge_slots()
        : proto::BlockSlotMask{};

    if (slot.core && ocram_sector().built_as(gen_))
        slot.routes.off_link_slots = ocram_sector().held()->bridge_slots();

    const bool events = slot.core && ocram_sector().built_as(gen_) && ocram_sector().held()->events;
    slot.routes.events = events ? Carrier::Ocram : Carrier::Spi;
    slot.routes.status = slot.core && sys().built_as(gen_)
        ? Carrier::Lw
        : (events ? Carrier::Ocram : Carrier::Spi);

    slot.routes.downloads = slot.core && download_lane_ && download_lane_->granted()
        ? Carrier::Ocram
        : Carrier::Spi;

    slot.frame_lane = slot.core ? grant_frame_lane_() : OcramFrameGrant{};
    slot.routes.frames = slot.frame_lane.granted() ? Carrier::Ocram : Carrier::Spi;

    slot.routes.declared_slots = ask.mount_slots.united(profile_slot_mask(profile));
    if (slot.routes.block_poll() == proto::link::BlockPollRoute::UntilIdle)
        ++counts_.poll_retirements;
    derive_enable_served_();
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

    if (held_[at]->routes.block_poll() == proto::link::BlockPollRoute::UntilIdle)
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
    if (lends_.any_mapped())
        return std::nullopt;
    drop_windows(receipt);
    return AllLeasesHome{};
}

std::optional<AllLeasesHome> CoreBuilder::nothing_lent() const noexcept {
    if (lends_.any_mapped() || counts_.builds != 0)
        return std::nullopt;
    return AllLeasesHome{};
}

void CoreBuilder::drop_windows(const ParkReceipt&) noexcept {

    enable_.drop();
    ocram_.drop();
    enable_header_.reset();
    for (CoreBay::Lease& l : held_) {
        if (!l)
            continue;
        for (auto& w : l->windows)
            w.reset();
        l->fed_ring.reset();
    }
    dir_reads_ = {};
    lane_.reset();
    drop_ocram_lane_();
}

Ex<CoreBuilder::Directory> CoreBuilder::directory_(
    const proto::AxiLwDecl& decl,
    hal::LwPresence lw_presence
) noexcept {
    DirReads::Entry* free_slot = nullptr;
    for (DirReads::Entry& e : dir_reads_.rows) {
        if (e.read && e.decl == decl)
            return *e.read;
        if (!e.read && !free_slot)
            free_slot = &e;
    }
    auto got = read_directory(terms_.lw_window, terms_.lw_image, decl, lw_presence);
    if (got) {
        ++directory_reads_;
    } else if (!lw_presence.lets_discover()) {

        if (lw_presence.firewall_held()) {
            ++counts_.lw_firewall_held_refusals;
        } else {
            ++counts_.lw_undiscovered_refusals;
        }
    }
    if (free_slot)
        *free_slot = DirReads::Entry{.decl = decl, .read = got};
    return got;
}

std::optional<CoreBuilder::DirHit> CoreBuilder::next_directory_(
    std::span<const proto::AxiLwDecl> rest,
    hal::LwPresence lw_presence
) noexcept {
    while (!rest.empty()) {
        const proto::AxiLwDecl decl = rest.front();
        rest = rest.subspan(1);
        if (!may_carry_pads(decl))
            continue;
        auto dir = directory_(decl, lw_presence);
        if (!dir)
            continue;
        return DirHit{.dir = *dir, .decl = decl, .rest = rest};
    }
    return std::nullopt;
}

std::optional<proto::link::LaneBlockDecl> CoreBuilder::find_lane_(
    std::span<const proto::AxiLwDecl> lw,
    hal::LwPresence lw_presence
) noexcept {
    for (auto hit = next_directory_(lw, lw_presence); hit;
         hit = next_directory_(hit->rest, lw_presence)) {
        if (auto lane = proto::link::find_lane_block(hit->dir.image, hit->decl.size))
            return *lane;
    }
    return std::nullopt;
}

void CoreBuilder::attach_enable_header_(const Directory& dir) noexcept {
    if (enable_.attached())
        return;

    if (terms_.lw_header) {
        enable_.attach(*terms_.lw_header);
        return;
    }
    auto header = dir.win.map<hal::SysDirectory>(hal::LwOffset{});
    if (!header)
        return enable_.attach_unmapped();
    enable_header_.emplace(std::move(*header));
    enable_.attach(enable_header_->lend());
}

void CoreBuilder::derive_enable_served_() noexcept {
    enable_.clear_masks();
    if (!enable_.attached())
        return;

    if (sector().built_as(gen_)) {
        enable_.note_grant(proto::DirEntryKind::Sector, sector().held()->block.index, true);
    }
    if (osd().built_as(gen_)) {
        enable_.note_grant(
            proto::DirEntryKind::Osd,
            osd().held()->block.index,
            osd().held()->block.line.declared()
        );
    }
    if (sys().built_as(gen_)) {
        enable_.note_grant(proto::DirEntryKind::Sys, sys().held()->block.index, true);
    }
    if (pad().built_as(gen_)) {
        enable_.note_grant(
            proto::DirEntryKind::Pad,
            pad().held()->block.index,
            pad().held()->block.line.declared()
        );
    }
}

void CoreBuilder::grant_pad_(
    std::span<const proto::AxiLwDecl> lw,
    const cores::CoreProfile& profile,
    const proto::SessionTerms& terms,
    FabricGeneration fabric_gen,
    proto::BuildGeneration gen,
    hal::LwPresence lw_presence
) noexcept {
    if (!pad().wired())
        return;
    if (!std::ranges::any_of(lw, may_carry_pads))
        return;

    if (!pad().retire()) {
        ++counts_.pad_refusals;
        return;
    }
    bool arm_absent = false;
    for (auto hit = next_directory_(lw, lw_presence); hit;
         hit = next_directory_(hit->rest, lw_presence)) {
        auto got = grant_pads(hit->dir, hit->decl);
        if (!got) {
            arm_absent = arm_absent
                || proto::link::pad_link_arm_absent(hit->dir.image, hit->decl.size);
            continue;
        }

        got->wire.analog_capable = terms.analog_capable;
        got->wire.suppress_analog_followup = profile.host.suppress_analog_followup;
        pad().install(LwLendKey{}, std::move(*got), LwGrantStamp::built_as(gen, fabric_gen));
        ++counts_.pad_grants;
        attach_enable_header_(hit->dir);
        return;
    }
    ++counts_.pad_refusals;

    if (arm_absent) {
        counts_.pad_no_arm.add();
        std::fprintf(
            stderr,
            "mister: LW PAD grant refused on a core built without the link's pad arm "
            "(PAD K9 bit 0): no pad input this session\n"
        );
    }
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
    const proto::AxiLwDecl& decl,
    hal::LwPresence lw
) noexcept {
    if (!lw.lets_discover())
        return std::unexpected(Error{Errc::busy, ERR_SITE(), 0});
    const auto window = hal::LwExtent::of_length(
        os::MapLength{lw_image.empty() ? lw_window.len : lw_image.size_bytes()}
    );
    if (!may_carry_pads(decl) || !window || !decl.offset.fits(*window, decl.size)) {
        return std::unexpected(decl.offset.refusal(Errc::aperture_range, ERR_SITE()));
    }
    auto win = discovered(lw_window, lw_image, decl);
    if (!win)
        return std::unexpected(win.error());
    auto dir = win->map<hal::SysDirectory>(hal::LwOffset{});
    if (!dir)
        return std::unexpected(dir.error());
    const proto::link::SysDirectoryImage image = proto::link::SysDirectoryImage::read(*dir);

    if (auto ok = image.valid_for(decl); !ok)
        return std::unexpected(ok.error());
    return Directory{*win, image};
}

Ex<LwPad> CoreBuilder::grant_pads(const Directory& dir, const proto::AxiLwDecl& decl) noexcept {
    auto block = proto::link::find_pad_block(dir.image, decl.size);
    if (!block)
        return std::unexpected(block.error());

    auto ident = dir.win.map<hal::PadIdent>(block->offset);
    if (!ident)
        return std::unexpected(ident.error());
    const hal::RegWord id = ident->read(hal::PadIdent::Reg::Id);
    if (id != hal::RegWord{hal::PadIdent::kId}) {
        return std::unexpected(id.refusal(Errc::bad_format, ERR_SITE()));
    }
    auto regs = dir.win.map<hal::UioRegFile>(block->offset);
    if (!regs)
        return std::unexpected(regs.error());
    return LwPad{.regs = std::move(*regs), .block = *block};
}

bool CoreBuilder::fabric_owned(
    const proto::IrqBinding& b,
    std::span<const proto::AxiLwDecl> lw
) noexcept {
    if (b.klass != proto::IrqClass::Blk || b.cause_base == hal::LwOffset{})
        return false;
    return std::ranges::any_of(lw, [&b](const proto::AxiLwDecl& d) noexcept {
        return may_carry_pads(d) && d.holds(b.cause_base);
    });
}

std::optional<hal::LwOffset> CoreBuilder::claim_sector(const Claim& c) noexcept {
    dir_reads_ = {};

    enable_.drop();
    enable_header_.reset();
    const cores::CoreProfile& profile = cores::profile_for(
        c.name,
        c.manifest ? cores::LoadHint::XmlManifest : cores::LoadHint::None
    );
    drop_unbuilt_sector_();
    grant_sector_(c.irqs, c.lw, profile, c.fabric_gen, c.lw_presence);
    claimed_ = true;
    publish_counts_();
    return unbuilt_cause_();
}

std::optional<hal::LwOffset> CoreBuilder::unbuilt_cause_() const noexcept {
    if (!sector().unbuilt() || sector().abandoned())
        return std::nullopt;

    return sector().held()->block.line.cause;
}

void CoreBuilder::drop_unbuilt_sector_() noexcept {
    if (sector().unbuilt())
        (void)sector().retire();
}

void CoreBuilder::grant_sector_(
    std::span<const proto::IrqBinding> irqs,
    std::span<const proto::AxiLwDecl> lw,
    const cores::CoreProfile& profile,
    FabricGeneration fabric_gen,
    hal::LwPresence lw_presence
) noexcept {
    const infra::OptRef<const svc::disc::ISectorProgram> program = profile.sector_program;
    if (!program || !sector().wired())
        return;

    if (sector_on_mailbox_(profile)) {
        (void)sector().retire();
        return;
    }
    if (!std::ranges::any_of(lw, may_carry_pads))
        return;

    if (!sector().retire()) {
        ++counts_.sector_refusals;
        return;
    }
    for (auto hit = next_directory_(lw, lw_presence); hit;
         hit = next_directory_(hit->rest, lw_presence)) {
        auto got = grant_sector(hit->dir, hit->decl, *program, source_(), terms_.doorbells, irqs);
        if (!got)
            continue;
        got->cards = grant_cards_(got->block, profile);
        sector().install(LwLendKey{}, std::move(*got), LwGrantStamp::claimed_on(fabric_gen));
        ++counts_.sector_grants;
        attach_enable_header_(hit->dir);
        return;
    }
    ++counts_.sector_refusals;
}

bool CoreBuilder::card_admitted(
    const cores::CoreProfile& profile,
    const proto::link::SectorBlockDecl::Card& card,
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
        && card.slot != block_slot;
}

std::array<LwSector::Card, proto::link::SectorBlockDecl::kCards> CoreBuilder::grant_cards_(
    const proto::link::SectorBlockDecl& block,
    const cores::CoreProfile& profile
) noexcept {
    std::array<LwSector::Card, proto::link::SectorBlockDecl::kCards> cards{};
    for (std::size_t c = 0; c < cards.size(); ++c) {
        const proto::link::SectorBlockDecl::Card& card = block.cards[c];
        if (!card.declared())
            continue;

        const bool slot_taken = std::ranges::any_of(
            cards,
            [&card](const LwSector::Card& g) noexcept {
                return g.decl.declared() && g.decl.slot == card.slot;
            }
        );
        if (slot_taken || !card_admitted(profile, card, block.slot)) {
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

Ex<hal::ApertureRegion> CoreBuilder::card_region_(
    const proto::link::SectorBlockDecl::Card& card
) const noexcept {
    return terms_.aperture.resolve(card.wsrc, card.extent.bytes(), "lw-card-image");
}

Ex<LwSector> CoreBuilder::grant_sector(
    const Directory& dir,
    const proto::AxiLwDecl& decl,
    const svc::disc::ISectorProgram& program,
    hal::IDoorbellSource& src,
    const hal::DoorbellPolicy& policy,
    std::span<const proto::IrqBinding> taken
) noexcept {
    auto found = proto::link::find_sector_block(dir.image, decl);
    if (!found)
        return std::unexpected(found.error());
    const proto::link::SectorBlockDecl& block = *found;

    if (block.slot != program.slot() || block.frame_bytes() != program.frame_bytes()) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), block.frame_words});
    }

    const hal::LwOffset at = decl.offset.plus(block.offset);
    if (block.line.cause != at) {
        return std::unexpected(block.line.cause.refusal(Errc::dt_missing, ERR_SITE()));
    }
    if (!policy.line_in_pool(block.line.line)) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), os::to_ordinal(block.line.line)}
        );
    }

    if (std::ranges::any_of(taken, [&](const proto::IrqBinding& b) {
            return b.line == block.line.line
                && !(b.cause_base == at
                     && fabric_owned(b, std::span<const proto::AxiLwDecl>(&decl, 1)));
        })) {
        return std::unexpected(Error{Errc::busy, ERR_SITE(), os::to_ordinal(block.line.line)});
    }
    auto regs = dir.win.map<hal::SectorBlock>(block.offset);
    if (!regs)
        return std::unexpected(regs.error());

    const hal::RegWord id = regs->read(hal::SectorBlock::Reg::Id);
    if (id != hal::RegWord{hal::SectorBlock::kId}) {
        return std::unexpected(id.refusal(Errc::bad_format, ERR_SITE()));
    }
    auto line = src.open_line(block.line.line);
    if (!line)
        return std::unexpected(line.error());
    return LwSector{
        .regs = std::move(*regs),
        .line = std::move(*line),
        .block = block,
        .program = infra::OptRef<const svc::disc::ISectorProgram>{program}
    };
}

bool CoreBuilder::bridge_refused() noexcept {
    bool refused = false;

    if (const auto a = sector().answered()) {

        const auto held = sector().held();
        refused = !a->bound
            || !held.has_value()
            || !held->card_slots().minus(a->cards_on_lw).empty();
    }

    if (const auto a = sys().answered())
        refused = refused || !a->bound;

    if (const auto a = ocram_sector().answered()) {
        const auto held = ocram_sector().held();
        refused = refused
            || !a->on_ocram
            || !held.has_value()
            || !held->mount_slot_mask().minus(a->cards_on_lw).empty()
            || (held->events && !a->notices);
    }

    if ((ocram_osd().built_as(gen_)
         || ocram_sector().built_as(gen_)
         || (download_lane_ && download_lane_->granted()))
        && ocram_.session_lost())
        refused = true;
    if (!refused)
        return false;
    ++counts_.bridge_refusals;
    publish_counts_();
    return true;
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

void CoreBuilder::grant_ocram_osd_(
    FabricGeneration fabric_gen,
    proto::BuildGeneration gen
) noexcept {
    if (!ocram_osd().wired())
        return;
    if (!ocram_osd().retire()) {
        ++counts_.osd_refusals;
        return;
    }
    const std::optional<hal::ocram::RingLend> ring = ocram_.lend_osd();
    const std::optional<proto::OsdRowCount> rows = ocram_.osd_rows();
    if (!ring || !rows) {
        ++counts_.osd_refusals;
        return;
    }
    ocram_osd().install(
        LwLendKey{},
        OcramOsd{.ring = *ring, .rows = *rows},
        LwGrantStamp::built_as(gen, fabric_gen)
    );
    ++counts_.osd_grants;
    ocram_.bind_osd();
}

void CoreBuilder::grant_ocram_lane_(std::span<const proto::IrqBinding> irqs) noexcept {
    drop_ocram_lane_();
    if (!download_lane_)
        return;
    const std::optional<hal::ocram::RingDesc> ring = ocram_.lane_ring();
    const std::optional<hal::ocram::RingLend> lend = ocram_.lend_lane();
    if (!ring || !lend)
        return;
    std::optional<os::UioLineRef> line{};
    const std::optional<os::UioLine> ordinal = ring->line();
    const auto claims = [&ordinal](const proto::IrqBinding& b) {
        return b.line == *ordinal;
    };
    const bool free_line = ordinal
        && terms_.doorbells.line_in_pool(*ordinal)
        && std::ranges::none_of(irqs, claims);
    if (free_line) {
        if (auto opened = source_().open_line(*ordinal)) {
            download_line_.emplace(std::move(*opened));
            line = download_line_->borrow();
        }
    }
    if (!download_lane_->bind(*lend, line)) {
        download_line_.reset();
        counts_.lane_refusals.add();
        return;
    }
    counts_.lane_grants.add();
}

OcramFrameGrant CoreBuilder::grant_frame_lane_() const noexcept {
    using Sel = hal::ocram::LaneArg::Sel;
    if (!download_lane_ || !download_lane_->granted())
        return OcramFrameGrant{};
    const std::optional<hal::ocram::LaneShape> shape = ocram_.lane_shape();
    if (!shape || !shape->serves(Sel::Uio) || !shape->serves(Sel::OsdVga))
        return OcramFrameGrant{};
    const std::optional<hal::ocram::RingLend> lend = ocram_.lend_lane();
    if (!lend)
        return OcramFrameGrant{};
    return OcramFrameGrant{.lend = *lend, .shape = shape};
}

void CoreBuilder::drop_ocram_lane_() noexcept {
    if (download_lane_)
        download_lane_->unbind();
    download_line_.reset();
}

bool CoreBuilder::sector_on_mailbox_(const cores::CoreProfile& profile) const noexcept {
    return profile.sector_program.has_value()
        && ocram_sector().wired()
        && ocram_.sector_rings().has_value();
}

void CoreBuilder::grant_ocram_sector_(
    std::span<const proto::IrqBinding> irqs,
    const cores::CoreProfile& profile,
    FabricGeneration fabric_gen,
    proto::BuildGeneration gen,
    proto::BindGeneration conf_gen
) noexcept {
    if (!ocram_sector().wired())
        return;
    if (!ocram_sector().retire()) {
        ++counts_.sector_refusals;
        return;
    }
    if (!sector_on_mailbox_(profile))
        return;
    const svc::disc::ISectorProgram& program = *profile.sector_program;
    const std::optional<OcramHostEnable::SectorRings> rings = ocram_.sector_rings();
    const std::optional<OcramHostEnable::SectorLends> lends = ocram_.lend_sector();
    const std::optional<os::UioLine> line = rings ? rings->evt.line() : std::nullopt;
    const bool fits = rings
        && lends
        && line
        && rings->shape.names(program.slot())
        && rings->shape.frame().bytes()
            == program.frame_bytes().window_count(proto::BlockCount{1u});
    const bool free_line = line
        && terms_.doorbells.line_in_pool(*line)
        && std::ranges::none_of(irqs, [&line](const proto::IrqBinding& b) {
                               return b.line == *line;
                           });
    if (!fits || !free_line) {
        ++counts_.sector_refusals;
        return;
    }
    auto opened = source_().open_line(*line);
    if (!opened) {
        ++counts_.sector_refusals;
        return;
    }

    const std::array<MountSlot, SectorGrantBody::kCards> mounts = rings->mounts
            && lends->mount.lent()
        ? grant_mounts_(*rings->mounts, profile)
        : std::array<MountSlot, SectorGrantBody::kCards>{};
    const bool any_mount = std::ranges::any_of(mounts, &MountSlot::granted);
    ocram_sector().install(
        LwLendKey{},
        OcramSector{
            .ring = lends->ring,
            .evt = lends->evt,
            .line = std::move(*opened),
            .program = profile.sector_program,
            .frame = rings->shape.frame(),
            .mount = any_mount ? lends->mount : hal::ocram::RingLend{},
            .mounts = mounts,
            .bind_records = rings->bind_records,
            .events = rings->events,
            .conf_gen = conf_gen
        },
        LwGrantStamp::built_as(gen, fabric_gen)
    );

    if (rings->events)
        ocram_.bind_events();
    ++counts_.sector_grants;
}

std::array<MountSlot, SectorGrantBody::kCards> CoreBuilder::grant_mounts_(
    const OcramHostEnable::MountRings& rings,
    const cores::CoreProfile& profile
) noexcept {
    std::array<MountSlot, SectorGrantBody::kCards> got{};
    const hal::ocram::Words room = rings.ring.words();
    for (std::size_t i = 0; i < got.size(); ++i) {
        const std::optional<hal::ocram::SectorShape>& shape = rings.slots[i];
        if (!shape)
            continue;
        const auto save = std::ranges::find_if(profile.slots, [&](const auto& s) noexcept {
            return shape->names(s.index.block_slot());
        });
        const bool admitted = !profile.saves_by_channel
            && save != profile.slots.end()
            && save->writable
            && save->role == cores::SlotRole::Save
            && !(profile.sector_program && shape->names(profile.sector_program->slot()))
            && hal::ocram::RecordHeader::frame(
                   hal::ocram::SlotArg::of(hal::ocram::kMountSlots[i]),
                   shape->frame()
               )
                    .extent()
                <= room;
        const bool slot_taken = std::ranges::any_of(got, [&](const MountSlot& g) noexcept {
            return g.granted() && save != profile.slots.end() && g.slot == save->index.block_slot();
        });
        if (!admitted || slot_taken) {
            ++counts_.card_refusals;
            continue;
        }
        got[i] = MountSlot{
            .arg = hal::ocram::SlotArg::of(hal::ocram::kMountSlots[i]),
            .slot = save->index.block_slot(),
            .frame = shape->frame()
        };
        ++counts_.card_grants;
    }
    return got;
}

void CoreBuilder::grant_osd_(
    std::span<const proto::AxiLwDecl> lw,
    const cores::CoreProfile& profile,
    FabricGeneration fabric_gen,
    proto::BuildGeneration gen,
    hal::LwPresence lw_presence
) noexcept {
    if (!osd().wired())
        return;
    if (!std::ranges::any_of(lw, may_carry_pads))
        return;

    if (!osd().retire()) {
        ++counts_.osd_refusals;
        return;
    }

    if (profile.host.front_end) {
        ++counts_.osd_refusals;
        return;
    }
    for (auto hit = next_directory_(lw, lw_presence); hit;
         hit = next_directory_(hit->rest, lw_presence)) {
        auto got = grant_osd(hit->dir, hit->decl);
        if (!got)
            continue;
        osd().install(LwLendKey{}, std::move(*got), LwGrantStamp::built_as(gen, fabric_gen));
        ++counts_.osd_grants;
        attach_enable_header_(hit->dir);
        return;
    }
    ++counts_.osd_refusals;
}

Ex<LwOsd> CoreBuilder::grant_osd(const Directory& dir, const proto::AxiLwDecl& decl) noexcept {
    auto block = proto::link::find_osd_block(dir.image, decl.size);
    if (!block)
        return std::unexpected(block.error());
    auto regs = dir.win.map<hal::OsdBlock>(block->offset);
    if (!regs)
        return std::unexpected(regs.error());
    const hal::RegWord id = regs->read(hal::OsdBlock::Reg::Id);
    if (id != hal::RegWord{hal::OsdBlock::kId}) {
        return std::unexpected(id.refusal(Errc::bad_format, ERR_SITE()));
    }
    return LwOsd{.regs = std::move(*regs), .block = *block};
}

void CoreBuilder::grant_sys_(
    std::span<const proto::IrqBinding> irqs,
    std::span<const proto::AxiLwDecl> lw,
    FabricGeneration fabric_gen,
    proto::BuildGeneration gen,
    hal::LwPresence lw_presence
) noexcept {
    if (!sys().wired())
        return;
    if (!std::ranges::any_of(lw, may_carry_pads))
        return;

    if (!sys().retire()) {
        ++counts_.sys_refusals;
        return;
    }

    const std::optional<os::UioLine> sector_line = sector().built_as(gen_)
        ? std::optional{sector().held()->block.line.line}
        : std::nullopt;
    for (auto hit = next_directory_(lw, lw_presence); hit;
         hit = next_directory_(hit->rest, lw_presence)) {
        auto got = grant_sys(hit->dir, hit->decl, source_(), terms_.doorbells, irqs, sector_line);
        if (!got)
            continue;
        pin_sys_line_(got->block.line.line);
        sys().install(LwLendKey{}, std::move(*got), LwGrantStamp::built_as(gen, fabric_gen));
        ++counts_.sys_grants;
        attach_enable_header_(hit->dir);
        return;
    }
    ++counts_.sys_refusals;
}

Ex<LwSys> CoreBuilder::grant_sys(
    const Directory& dir,
    const proto::AxiLwDecl& decl,
    hal::IDoorbellSource& src,
    const hal::DoorbellPolicy& policy,
    std::span<const proto::IrqBinding> taken,
    std::optional<os::UioLine> held
) noexcept {
    auto found = proto::link::find_sys_block(dir.image, decl);
    if (!found)
        return std::unexpected(found.error());
    const proto::link::SysBlockDecl& block = *found;

    const hal::LwOffset at = decl.offset.plus(block.offset);
    if (block.line.cause != at) {
        return std::unexpected(block.line.cause.refusal(Errc::dt_missing, ERR_SITE()));
    }
    if (!policy.line_in_pool(block.line.line)) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), os::to_ordinal(block.line.line)}
        );
    }

    if (held == block.line.line || std::ranges::any_of(taken, [&](const proto::IrqBinding& b) {
            return b.line == block.line.line;
        })) {
        return std::unexpected(Error{Errc::busy, ERR_SITE(), os::to_ordinal(block.line.line)});
    }
    auto regs = dir.win.map<hal::SysBlock>(block.offset);
    if (!regs)
        return std::unexpected(regs.error());
    const hal::RegWord id = regs->read(hal::SysBlock::Reg::Id);
    if (id != hal::RegWord{hal::SysBlock::kId}) {
        return std::unexpected(id.refusal(Errc::bad_format, ERR_SITE()));
    }
    auto line = src.open_line(block.line.line);
    if (!line)
        return std::unexpected(line.error());
    return LwSys{.regs = std::move(*regs), .line = std::move(*line), .block = block};
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
        terms_.doorbell_nodes.f2h_of(line).text().view()
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

void CoreBuilder::count_lease_hold(bool long_hold) noexcept {
    if (long_hold) {
        ++counts_.lease_long_holds;
    } else {
        ++counts_.lease_holds;
    }
    publish_counts_();
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
    b.save_store.reset();
    b.disk_servants = {};
    b.slot_servants = {};
    for (auto& w : b.windows)
        w.reset();
    b.fed_ring.reset();
    b.routes = SessionRoutes{};
    b.gen = {};
    held_[slot].reset();
    ++counts_.frees;
    publish_counts_();
}

void CoreBuilder::publish_counts_() noexcept {
    if (download_lane_)
        counts_.lane_busy = download_lane_->counts().busy;
    counts_cell_.publish(counts_);
}

}
