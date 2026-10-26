// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include "app/all_leases_home.h"
#include "app/bay_counts.h"
#include "app/bay_releases.h"
#include "app/block_poll_route.h"
#include "app/built_core.h"
#include "app/core_terms.h"
#include "app/doorbell_lines.h"
#include "app/lw_osd.h"
#include "app/lw_sector.h"
#include "app/lw_sys.h"
#include "app/osd_grant.h"
#include "app/osd_route.h"
#include "app/park_receipt.h"
#include "app/sector_grant.h"
#include "app/sector_route.h"
#include "app/sys_grant.h"
#include "app/sys_route.h"
#include "cores/core_manifest.h"
#include "cores/core_window_grant.h"
#include "hal/doorbell_policy.h"
#include "hal/doorbell_source.h"
#include "hal/lw_sys_window.h"
#include "hal/sys_directory.h"
#include "hal/uio_doorbell_source.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "infra/wake_flag.h"
#include "proto/axi_lw_decl.h"
#include "proto/irq_binding.h"
#include "proto/lane_block_decl.h"
#include "proto/link_op.h"
#include "proto/sys_directory_image.h"
#include "svc/sector_program.h"

namespace mister::cores {
struct CoreProfile;
class Core;
}

namespace mister::app {

class CoreBuilder {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct Ask {
        std::string_view name;
        cores::CoreManifest manifest{};
        std::span<const proto::IrqBinding> irqs{};
        std::span<const proto::AxiLwDecl> lw{};

        bool fabric = false;
        std::uint32_t fabric_gen = 0;
        std::uint16_t mount_slots = 0;
    };

    CoreBuilder(CoreBay& bay, const BayReleasesCell& releases, const CoreTerms& terms) noexcept
        : CoreBuilder(bay, releases, terms, infra::OptRef<hal::IDoorbellSource>{}) {}

    CoreBuilder(
        CoreBay& bay,
        const BayReleasesCell& releases,
        const CoreTerms& terms,
        hal::IDoorbellSource& src
    ) noexcept
        : CoreBuilder(bay, releases, terms, infra::OptRef<hal::IDoorbellSource>{src}) {}

    CoreBuilder(
        CoreBay& bay,
        const BayReleasesCell& releases,
        const CoreTerms& terms,
        infra::OptRef<hal::IDoorbellSource> src
    ) noexcept
        : CoreBuilder(bay, releases, terms, src, SectorCells{}) {}

    struct SectorCells {
        infra::OptRef<SectorGrantCell> grant{};
        infra::OptRef<const SectorRouteCell> route{};
        infra::OptRef<xthread::WakeFlag> wake{};
    };

    struct OsdCells {
        infra::OptRef<OsdGrantCell> grant{};
        infra::OptRef<const OsdRouteCell> route{};
    };

    CoreBuilder(
        CoreBay& bay,
        const BayReleasesCell& releases,
        const CoreTerms& terms,
        infra::OptRef<hal::IDoorbellSource> src,
        SectorCells sectors
    ) noexcept
        : CoreBuilder(bay, releases, terms, src, sectors, OsdCells{}) {}

    CoreBuilder(
        CoreBay& bay,
        const BayReleasesCell& releases,
        const CoreTerms& terms,
        infra::OptRef<hal::IDoorbellSource> src,
        SectorCells sectors,
        OsdCells osds
    ) noexcept
        : CoreBuilder(bay, releases, terms, src, sectors, osds, SysCells{}) {}

    struct SysCells {
        infra::OptRef<SysGrantCell> grant{};
        infra::OptRef<const SysRouteCell> route{};
        infra::OptRef<xthread::WakeFlag> wake{};
    };

    CoreBuilder(
        CoreBay& bay,
        const BayReleasesCell& releases,
        const CoreTerms& terms,
        infra::OptRef<hal::IDoorbellSource> src,
        SectorCells sectors,
        OsdCells osds,
        SysCells sys
    ) noexcept;
    CoreBuilder(const CoreBuilder&) = delete;
    CoreBuilder& operator=(const CoreBuilder&) = delete;
    ~CoreBuilder();

    struct Claim {
        std::string_view name;
        bool manifest = false;
        std::span<const proto::IrqBinding> irqs{};
        std::span<const proto::AxiLwDecl> lw{};
        std::uint32_t fabric_gen = 0;
    };

    [[nodiscard]] std::optional<std::uint32_t> claim_sector(const Claim& c) noexcept;

    [[nodiscard]] proto::LinkOp::AdoptCore build(const Ask& ask);

    void forget(const proto::LinkOp::AdoptCore& op) noexcept;

    void reap() noexcept;

    [[nodiscard]] std::optional<AllLeasesHome> leases_home(const ParkReceipt& receipt) noexcept;

    [[nodiscard]] std::optional<AllLeasesHome> nothing_lent() const noexcept;

    [[nodiscard]] cores::CoreWindowGrant grant_windows(
        BuiltCore& into,
        const cores::CoreProfile& profile
    ) noexcept;

    static void open_doorbells(
        hal::IDoorbellSource& src,
        const hal::DoorbellPolicy& policy,
        std::span<const proto::IrqBinding> irqs,
        DoorbellLines& into
    ) noexcept;

    [[nodiscard]] std::uint32_t builds() const noexcept { return builds_; }

    [[nodiscard]] std::uint32_t bay_full() const noexcept { return bay_full_; }

    [[nodiscard]] std::uint32_t frees() const noexcept { return frees_; }

    [[nodiscard]] std::uint32_t window_refusals() const noexcept { return window_refusals_; }

    [[nodiscard]] std::uint32_t pad_grants() const noexcept { return pad_grants_; }

    [[nodiscard]] std::uint32_t pad_refusals() const noexcept { return pad_refusals_; }

    [[nodiscard]] static constexpr bool may_carry_pads(const proto::AxiLwDecl& decl) noexcept {
        return decl.size >= hal::SysDirectory::kSize;
    }

    struct Directory {
        hal::LwSysWindow win;
        proto::SysDirectoryImage image;
    };

    [[nodiscard]] static Ex<Directory> read_directory(
        hal::PhysRegion lw_window,
        std::span<std::uint32_t> lw_image,
        const proto::AxiLwDecl& decl
    ) noexcept;

    [[nodiscard]] static Ex<LwPads> grant_pads(
        hal::PhysRegion lw_window,
        std::span<std::uint32_t> lw_image,
        const proto::AxiLwDecl& decl
    ) noexcept;
    [[nodiscard]] static Ex<LwPads> grant_pads(
        const Directory& dir,
        const proto::AxiLwDecl& decl
    ) noexcept;

    [[nodiscard]] const std::optional<proto::LaneBlockDecl>& lane() const noexcept { return lane_; }

    [[nodiscard]] std::uint32_t directory_reads() const noexcept { return directory_reads_; }

    [[nodiscard]] std::uint32_t sector_grants() const noexcept { return sector_grants_; }

    [[nodiscard]] std::uint32_t sector_refusals() const noexcept { return sector_refusals_; }

    [[nodiscard]] std::uint32_t card_grants() const noexcept { return card_grants_; }

    [[nodiscard]] std::uint32_t card_refusals() const noexcept { return card_refusals_; }

    [[nodiscard]] static bool card_admitted(
        const cores::CoreProfile& profile,
        const proto::SectorBlockDecl::Card& card,
        std::uint8_t block_slot
    ) noexcept;

    [[nodiscard]] std::uint32_t lease_holds() const noexcept { return lease_holds_; }

    [[nodiscard]] std::uint32_t lease_long_holds() const noexcept { return lease_long_holds_; }

    void count_lease_hold(bool long_hold) noexcept;

    [[nodiscard]] static bool fabric_owned(
        const proto::IrqBinding& b,
        std::span<const proto::AxiLwDecl> lw
    ) noexcept;

    static std::size_t rt_rows(
        std::span<const proto::IrqBinding> irqs,
        std::span<const proto::AxiLwDecl> lw,
        std::span<proto::IrqBinding> out,
        std::optional<std::uint32_t> owned
    ) noexcept;

    [[nodiscard]] static Ex<LwSector> grant_sector(
        hal::PhysRegion lw_window,
        std::span<std::uint32_t> lw_image,
        const proto::AxiLwDecl& decl,
        std::span<const proto::IrqBinding> irqs,
        const svc::ISectorProgram& program,
        hal::IDoorbellSource& src,
        const hal::DoorbellPolicy& policy
    ) noexcept;
    [[nodiscard]] static Ex<LwSector> grant_sector(
        const Directory& dir,
        const proto::AxiLwDecl& decl,
        std::span<const proto::IrqBinding> irqs,
        const svc::ISectorProgram& program,
        hal::IDoorbellSource& src,
        const hal::DoorbellPolicy& policy
    ) noexcept;

    [[nodiscard]] static BlockPollRoute block_poll_route(
        std::uint16_t declared,
        std::uint16_t lw_slots
    ) noexcept {
        return lw_slots != 0 && (declared & static_cast<std::uint16_t>(~lw_slots)) == 0
            ? BlockPollRoute::Retired
            : BlockPollRoute::Link;
    }

    [[nodiscard]] static std::uint16_t profile_slot_mask(
        const cores::CoreProfile& profile
    ) noexcept;

    [[nodiscard]] std::uint32_t poll_retirements() const noexcept { return poll_retirements_; }

    [[nodiscard]] static bool fabric_route(std::string_view name, bool manifest) noexcept;

    void raise_sector() noexcept;

    [[nodiscard]] bool retire_sector(bool fabric_ok) noexcept;

    void abandon_sector() noexcept;

    [[nodiscard]] bool sector_mapped() const noexcept { return sector_.has_value(); }

    [[nodiscard]] bool sector_abandoned() const noexcept { return sector_ && sector_->fabric_lost; }

    [[nodiscard]] bool bridge_refused() noexcept;

    [[nodiscard]] std::uint32_t bridge_refusals() const noexcept { return bridge_refusals_; }

    [[nodiscard]] std::uint32_t osd_grants() const noexcept { return osd_grants_; }

    [[nodiscard]] std::uint32_t osd_refusals() const noexcept { return osd_refusals_; }

    [[nodiscard]] static Ex<LwOsd> grant_osd(
        hal::PhysRegion lw_window,
        std::span<std::uint32_t> lw_image,
        const proto::AxiLwDecl& decl
    ) noexcept;
    [[nodiscard]] static Ex<LwOsd> grant_osd(
        const Directory& dir,
        const proto::AxiLwDecl& decl
    ) noexcept;

    void raise_osd() noexcept;

    [[nodiscard]] bool retire_osd(bool fabric_ok) noexcept;

    void abandon_osd() noexcept;

    [[nodiscard]] bool osd_mapped() const noexcept { return osd_.has_value(); }

    [[nodiscard]] bool osd_abandoned() const noexcept { return osd_ && osd_->fabric_lost; }

    [[nodiscard]] std::uint32_t sys_grants() const noexcept { return sys_grants_; }

    [[nodiscard]] std::uint32_t sys_refusals() const noexcept { return sys_refusals_; }

    [[nodiscard]] std::uint32_t sys_pin_refusals() const noexcept { return sys_pin_refusals_; }

    [[nodiscard]] static Ex<LwSys> grant_sys(
        hal::PhysRegion lw_window,
        std::span<std::uint32_t> lw_image,
        const proto::AxiLwDecl& decl,
        hal::IDoorbellSource& src,
        const hal::DoorbellPolicy& policy,
        std::span<const proto::IrqBinding> taken = {}
    ) noexcept;
    [[nodiscard]] static Ex<LwSys> grant_sys(
        const Directory& dir,
        const proto::AxiLwDecl& decl,
        hal::IDoorbellSource& src,
        const hal::DoorbellPolicy& policy,
        std::span<const proto::IrqBinding> taken = {}
    ) noexcept;

    void raise_sys() noexcept;

    [[nodiscard]] bool retire_sys() noexcept;

    void abandon_sys() noexcept;

    [[nodiscard]] bool sys_mapped() const noexcept { return sys_.has_value(); }

    [[nodiscard]] std::uint32_t leased() const noexcept;

    [[nodiscard]] const BayCountsCell& counts_cell() const noexcept { return counts_cell_; }

private:
    void drop_windows(const ParkReceipt& receipt) noexcept;
    void free_(std::uint8_t slot) noexcept;
    void publish_counts_() noexcept;

    [[nodiscard]] hal::IDoorbellSource& doorbell_source_() noexcept;
    void open_doorbells_(BuiltCore& into, std::span<const proto::IrqBinding> irqs) noexcept;
    [[nodiscard]] Ex<Directory> directory_(const proto::AxiLwDecl& decl) noexcept;
    [[nodiscard]] std::optional<proto::LaneBlockDecl> find_lane_(
        std::span<const proto::AxiLwDecl> lw
    ) noexcept;
    void grant_pads_(BuiltCore& into, std::span<const proto::AxiLwDecl> lw) noexcept;

    void grant_sector_(
        std::span<const proto::IrqBinding> irqs,
        std::span<const proto::AxiLwDecl> lw,
        const cores::CoreProfile& profile,
        std::uint32_t fabric_gen
    ) noexcept;
    void grant_cards_(LwSector& into, const cores::CoreProfile& profile) noexcept;
    [[nodiscard]] Ex<hal::PhysRegion> card_region_(
        const proto::SectorBlockDecl::Card& card
    ) const noexcept;

    void drop_unbuilt_sector_() noexcept;
    [[nodiscard]] std::optional<std::uint32_t> unbuilt_cause_() const noexcept;
    [[nodiscard]] bool sector_leased_() const noexcept;
    void revoke_sector_() noexcept;
    void grant_osd_(
        std::span<const proto::AxiLwDecl> lw,
        const cores::CoreProfile& profile,
        std::uint32_t fabric_gen,
        std::uint16_t gen
    ) noexcept;
    [[nodiscard]] bool osd_leased_() const noexcept;
    void revoke_osd_() noexcept;
    void grant_sys_(
        std::span<const proto::IrqBinding> irqs,
        std::span<const proto::AxiLwDecl> lw,
        std::uint32_t fabric_gen,
        std::uint16_t gen
    ) noexcept;

    void pin_sys_line_(os::UioLine line) noexcept;
    [[nodiscard]] bool sys_leased_() const noexcept;
    void revoke_sys_() noexcept;

    CoreBay& bay_;
    const BayReleasesCell& releases_;
    const CoreTerms& terms_;
    hal::UioDoorbellSource uio_doorbells_{};
    hal::IDoorbellSource& doorbells_;
    BayReleasesCell::Reader released_{};
    BayReleases levels_{};
    std::array<CoreBay::Lease, kCoreBaySlots> held_{};
    std::uint16_t gen_ = 0;
    std::uint32_t builds_ = 0;
    std::uint32_t bay_full_ = 0;
    std::uint32_t frees_ = 0;
    std::uint32_t window_refusals_ = 0;
    std::uint32_t pad_grants_ = 0;
    std::uint32_t pad_refusals_ = 0;
    std::uint32_t sector_grants_ = 0;
    std::uint32_t sector_refusals_ = 0;
    std::uint32_t card_grants_ = 0;
    std::uint32_t card_refusals_ = 0;
    bool claimed_ = false;
    std::uint32_t lease_holds_ = 0;
    std::uint32_t lease_long_holds_ = 0;
    SectorCells sectors_{};
    std::optional<LwSector> sector_{};
    bool sector_revoked_ = true;

    std::uint16_t lent_gen_ = 0;
    std::uint32_t lent_cards_ = 0;
    std::uint16_t judged_gen_ = 0;
    std::uint32_t bridge_refusals_ = 0;
    std::uint32_t poll_retirements_ = 0;
    std::uint32_t osd_grants_ = 0;
    std::uint32_t osd_refusals_ = 0;
    OsdCells osds_{};
    std::optional<LwOsd> osd_{};
    bool osd_revoked_ = true;
    SysCells sys_cells_{};
    std::optional<LwSys> sys_{};
    bool sys_revoked_ = true;
    std::uint16_t lent_sys_gen_ = 0;
    std::uint16_t judged_sys_gen_ = 0;
    std::uint32_t sys_grants_ = 0;
    std::uint32_t sys_refusals_ = 0;
    std::uint32_t sys_pin_refusals_ = 0;
    std::uint32_t directory_reads_ = 0;

    struct DirReads {
        struct Entry {
            std::uint32_t offset = 0;
            std::uint32_t size = 0;
            std::optional<Ex<Directory>> read{};
        };

        std::array<Entry, 4> rows{};
    };

    DirReads dir_reads_{};
    std::optional<proto::LaneBlockDecl> lane_{};
    BayCountsCell counts_cell_{};
};

}
