// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include "app/all_leases_home.h"
#include "app/bay_counts.h"
#include "app/bay_releases.h"
#include "app/types.h"
#include "app/built_core.h"
#include "app/core_terms.h"
#include "app/lw_block_lend.h"
#include "app/lw_host_enable.h"
#include "app/lw_lend_set.h"
#include "app/lw_osd.h"
#include "app/ocram_host_enable.h"
#include "app/ocram_lane_writer.h"
#include "app/ocram_osd.h"
#include "app/ocram_sector.h"
#include "app/lw_pad.h"
#include "app/lw_sector.h"
#include "app/lw_sys.h"
#include "app/mount_slot.h"
#include "app/osd_grant.h"
#include "app/osd_route.h"
#include "app/pad_grant.h"
#include "app/pad_route.h"
#include "app/park_receipt.h"
#include "app/sector_grant.h"
#include "app/sector_route.h"
#include "app/sys_grant.h"
#include "app/sys_route.h"
#include "cores/core_manifest.h"
#include "cores/core_window_grant.h"
#include "hal/doorbell_policy.h"
#include "hal/doorbell_source.h"
#include "hal/lw_presence.h"
#include "hal/ocram_presence.h"
#include "hal/lw_sys_window.h"
#include "hal/ocram_platform.h"
#include "hal/sys_directory.h"
#include "hal/board_doorbell_source.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "infra/xthread/wake_flag.h"
#include "proto/axi_lw_decl.h"
#include "proto/build_generation.h"
#include "proto/irq_binding.h"
#include "proto/link/lane_block_decl.h"
#include "proto/link_op.h"
#include "proto/link/sys_directory_image.h"
#include "proto/session_terms.h"
#include "svc/disc/sector_program.h"

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

        hal::LwPresence lw_presence{};

        hal::OcramPresence ocram_presence{};

        bool fabric = false;
        FabricGeneration fabric_gen{};
        proto::BlockSlotMask mount_slots{};
        proto::SessionTerms terms{};

        proto::BindGeneration conf_gen{};
    };

    struct SectorCells {
        SectorGrantCell& grant;
        const SectorRouteCell& route;
        const SectorLeaseCell& lease;
        infra::OptRef<xthread::WakeFlag> wake{};
    };

    struct OsdCells {
        OsdGrantCell& grant;
        const OsdLeaseCell& lease;
    };

    struct SysCells {
        SysGrantCell& grant;
        const SysRouteCell& route;
        const SysLeaseCell& lease;
        infra::OptRef<xthread::WakeFlag> wake{};
    };

    struct PadCells {
        PadGrantCell& grant;
        const PadLeaseCell& lease;
        infra::OptRef<xthread::WakeFlag> wake{};
    };

    using SectorLend = LwBlockLend<LwSector, SectorCells>;
    using OsdLend = LwBlockLend<LwOsd, OsdCells>;
    using OcramOsdLend = LwBlockLend<OcramOsd, OsdCells>;
    using OcramSectorLend = LwBlockLend<OcramSector, SectorCells>;
    using SysLend = LwBlockLend<LwSys, SysCells>;
    using PadLend = LwBlockLend<LwPad, PadCells>;

    using LwLends = LwLendSet<SectorLend, OcramSectorLend, OsdLend, OcramOsdLend, SysLend, PadLend>;

    struct Wiring {
        CoreBay& bay;
        const BayReleasesCell& releases;
        const CoreTerms& terms;

        hal::PhysRegion ocram_window{};
        infra::OptRef<hal::IOcramPlatform> ocram_platform{};
        infra::OptRef<hal::ocram::Window> ocram_mapped{};
        infra::OptRef<os::IDelay> ocram_delay{};

        infra::OptRef<OcramLaneWriter> download_lane{};
        std::optional<SectorCells> sectors{};
        std::optional<OsdCells> osds{};
        std::optional<SysCells> sys{};
        std::optional<PadCells> pads{};
    };

    [[nodiscard]] static CoreBuilder on_board(Wiring wiring) noexcept;

    [[nodiscard]] static CoreBuilder over_source(Wiring wiring, hal::IDoorbellSource& src) noexcept;
    CoreBuilder(const CoreBuilder&) = delete;
    CoreBuilder& operator=(const CoreBuilder&) = delete;
    ~CoreBuilder();

    struct Claim {
        std::string_view name;
        bool manifest = false;
        std::span<const proto::IrqBinding> irqs{};
        std::span<const proto::AxiLwDecl> lw{};
        hal::LwPresence lw_presence{};
        FabricGeneration fabric_gen{};
    };

    [[nodiscard]] std::optional<hal::LwOffset> claim_sector(const Claim& c) noexcept;

    [[nodiscard]] proto::LinkOp::AdoptCore build(const Ask& ask);

    void forget(const proto::LinkOp::AdoptCore& op) noexcept;

    void reap() noexcept;

    [[nodiscard]] std::optional<AllLeasesHome> leases_home(const ParkReceipt& receipt) noexcept;

    [[nodiscard]] std::optional<AllLeasesHome> nothing_lent() const noexcept;

    struct GrantedWindows {

        struct Row {
            std::string_view name{};
            std::optional<CoreWindow> mapped{};
            infra::OptRef<cores::ICoreWindow> borrowed{};
        };

        std::array<Row, cores::kMaxCoreWindows> rows{};
        std::optional<hal::FpgaMemory> fed_ring{};
    };

    [[nodiscard]] GrantedWindows grant_windows(const cores::CoreProfile& profile) noexcept;

    [[nodiscard]] static cores::CoreWindowGrant adopt_windows(
        BuiltCore& slot,
        GrantedWindows windows
    ) noexcept;

    [[nodiscard]] static constexpr bool may_carry_pads(const proto::AxiLwDecl& decl) noexcept {
        return decl.size.covers(hal::LwExtent::of_layout<hal::SysDirectory>());
    }

    struct Directory {
        hal::LwSysWindow win;
        proto::link::SysDirectoryImage image;
    };

    [[nodiscard]] static Ex<Directory> read_directory(
        hal::PhysRegion lw_window,
        std::span<std::uint32_t> lw_image,
        const proto::AxiLwDecl& decl,
        hal::LwPresence lw
    ) noexcept;

    [[nodiscard]] static Ex<LwPad> grant_pads(
        const Directory& dir,
        const proto::AxiLwDecl& decl
    ) noexcept;

    [[nodiscard]] const std::optional<proto::link::LaneBlockDecl>& lane() const noexcept {
        return lane_;
    }

    [[nodiscard]] std::uint32_t directory_reads() const noexcept { return directory_reads_; }

    [[nodiscard]] static bool card_admitted(
        const cores::CoreProfile& profile,
        const proto::link::SectorBlockDecl::Card& card,
        proto::SlotIndex block_slot
    ) noexcept;
    void count_lease_hold(bool long_hold) noexcept;

    [[nodiscard]] static bool fabric_owned(
        const proto::IrqBinding& b,
        std::span<const proto::AxiLwDecl> lw
    ) noexcept;

    [[nodiscard]] static Ex<LwSector> grant_sector(
        const Directory& dir,
        const proto::AxiLwDecl& decl,
        const svc::disc::ISectorProgram& program,
        hal::IDoorbellSource& src,
        const hal::DoorbellPolicy& policy,
        std::span<const proto::IrqBinding> taken = {}
    ) noexcept;

    [[nodiscard]] static proto::BlockSlotMask profile_slot_mask(
        const cores::CoreProfile& profile
    ) noexcept;

    [[nodiscard]] static bool fabric_route(std::string_view name, bool manifest) noexcept;

    [[nodiscard]] SectorLend& sector() noexcept { return lends_.get<SectorLend>(); }

    [[nodiscard]] const SectorLend& sector() const noexcept { return lends_.get<SectorLend>(); }

    [[nodiscard]] proto::BuildGeneration generation() const noexcept { return gen_; }

    [[nodiscard]] bool bridge_refused() noexcept;

    [[nodiscard]] static Ex<LwOsd> grant_osd(
        const Directory& dir,
        const proto::AxiLwDecl& decl
    ) noexcept;

    [[nodiscard]] OsdLend& osd() noexcept { return lends_.get<OsdLend>(); }

    [[nodiscard]] const OsdLend& osd() const noexcept { return lends_.get<OsdLend>(); }

    [[nodiscard]] OcramOsdLend& ocram_osd() noexcept { return lends_.get<OcramOsdLend>(); }

    [[nodiscard]] const OcramOsdLend& ocram_osd() const noexcept {
        return lends_.get<OcramOsdLend>();
    }

    [[nodiscard]] OcramSectorLend& ocram_sector() noexcept { return lends_.get<OcramSectorLend>(); }

    [[nodiscard]] const OcramSectorLend& ocram_sector() const noexcept {
        return lends_.get<OcramSectorLend>();
    }

    [[nodiscard]] OcramHostEnable& ocram() noexcept { return ocram_; }

    [[nodiscard]] const OcramHostEnable& ocram() const noexcept { return ocram_; }

    [[nodiscard]] static Ex<LwSys> grant_sys(
        const Directory& dir,
        const proto::AxiLwDecl& decl,
        hal::IDoorbellSource& src,
        const hal::DoorbellPolicy& policy,
        std::span<const proto::IrqBinding> taken = {},
        std::optional<os::UioLine> held = {}
    ) noexcept;

    [[nodiscard]] SysLend& sys() noexcept { return lends_.get<SysLend>(); }

    [[nodiscard]] const SysLend& sys() const noexcept { return lends_.get<SysLend>(); }

    [[nodiscard]] PadLend& pad() noexcept { return lends_.get<PadLend>(); }

    [[nodiscard]] const PadLend& pad() const noexcept { return lends_.get<PadLend>(); }

    [[nodiscard]] LwLends& lends() noexcept { return lends_; }

    [[nodiscard]] const LwLends& lends() const noexcept { return lends_; }

    [[nodiscard]] LwHostEnable& lw_enable() noexcept { return enable_; }

    [[nodiscard]] const LwHostEnable& lw_enable() const noexcept { return enable_; }

    [[nodiscard]] std::uint32_t leased() const noexcept;

    [[nodiscard]] const BayCounts& counts() const noexcept { return counts_; }

    [[nodiscard]] const BayCountsCell& counts_cell() const noexcept { return counts_cell_; }

private:
    CoreBuilder(Wiring wiring, infra::OptRef<hal::IDoorbellSource> seam) noexcept;

    [[nodiscard]] hal::IDoorbellSource& source_() noexcept;

    void drop_windows(const ParkReceipt& receipt) noexcept;
    void free_(proto::CoreBaySlot slot) noexcept;
    void publish_counts_() noexcept;
    [[nodiscard]] Ex<Directory> directory_(
        const proto::AxiLwDecl& decl,
        hal::LwPresence lw_presence
    ) noexcept;

    struct DirHit {
        Directory dir;
        proto::AxiLwDecl decl;
        std::span<const proto::AxiLwDecl> rest;
    };

    [[nodiscard]] std::optional<DirHit> next_directory_(
        std::span<const proto::AxiLwDecl> rest,
        hal::LwPresence lw_presence
    ) noexcept;
    [[nodiscard]] std::optional<proto::link::LaneBlockDecl> find_lane_(
        std::span<const proto::AxiLwDecl> lw,
        hal::LwPresence lw_presence
    ) noexcept;

    void grant_pad_(
        std::span<const proto::AxiLwDecl> lw,
        const cores::CoreProfile& profile,
        const proto::SessionTerms& terms,
        FabricGeneration fabric_gen,
        proto::BuildGeneration gen,
        hal::LwPresence lw_presence
    ) noexcept;

    void grant_sector_(
        std::span<const proto::IrqBinding> irqs,
        std::span<const proto::AxiLwDecl> lw,
        const cores::CoreProfile& profile,
        FabricGeneration fabric_gen,
        hal::LwPresence lw_presence
    ) noexcept;

    [[nodiscard]] std::array<LwSector::Card, proto::link::SectorBlockDecl::kCards> grant_cards_(
        const proto::link::SectorBlockDecl& block,
        const cores::CoreProfile& profile
    ) noexcept;
    [[nodiscard]] Ex<hal::ApertureRegion> card_region_(
        const proto::link::SectorBlockDecl::Card& card
    ) const noexcept;

    void drop_unbuilt_sector_() noexcept;
    [[nodiscard]] std::optional<hal::LwOffset> unbuilt_cause_() const noexcept;
    void grant_osd_(
        std::span<const proto::AxiLwDecl> lw,
        const cores::CoreProfile& profile,
        FabricGeneration fabric_gen,
        proto::BuildGeneration gen,
        hal::LwPresence lw_presence
    ) noexcept;

    void grant_ocram_osd_(FabricGeneration fabric_gen, proto::BuildGeneration gen) noexcept;

    void grant_ocram_lane_(std::span<const proto::IrqBinding> irqs) noexcept;
    void drop_ocram_lane_() noexcept;
    [[nodiscard]] OcramFrameGrant grant_frame_lane_() const noexcept;

    void grant_ocram_sector_(
        std::span<const proto::IrqBinding> irqs,
        const cores::CoreProfile& profile,
        FabricGeneration fabric_gen,
        proto::BuildGeneration gen,
        proto::BindGeneration conf_gen
    ) noexcept;

    [[nodiscard]] bool sector_on_mailbox_(const cores::CoreProfile& profile) const noexcept;

    [[nodiscard]] std::array<MountSlot, SectorGrantBody::kCards> grant_mounts_(
        const OcramHostEnable::MountRings& rings,
        const cores::CoreProfile& profile
    ) noexcept;
    void grant_sys_(
        std::span<const proto::IrqBinding> irqs,
        std::span<const proto::AxiLwDecl> lw,
        FabricGeneration fabric_gen,
        proto::BuildGeneration gen,
        hal::LwPresence lw_presence
    ) noexcept;

    void attach_enable_header_(const Directory& dir) noexcept;

    void derive_enable_served_() noexcept;

    static constexpr std::size_t kNodeNameCap = 48;

    void pin_sys_line_(os::UioLine line) noexcept;

    CoreBay& bay_;
    const BayReleasesCell& releases_;
    const CoreTerms& terms_;

    infra::OptRef<hal::IDoorbellSource> seam_{};

    hal::BoardDoorbellSource uio_doorbells_{};
    BayReleasesCell::Reader released_{};
    BayReleases levels_{};
    infra::IndexedArray<CoreBay::Lease, kCoreBaySlots, proto::CoreBaySlot> held_{};
    proto::BuildGeneration gen_{};
    bool claimed_ = false;

    LwLends lends_;

    std::uint32_t directory_reads_ = 0;

    struct DirReads {
        struct Entry {
            proto::AxiLwDecl decl{};
            std::optional<Ex<Directory>> read{};
            friend constexpr bool operator==(const Entry&, const Entry&) = default;
        };

        std::array<Entry, 4> rows{};
    };

    DirReads dir_reads_{};
    std::optional<proto::link::LaneBlockDecl> lane_{};

    BayCounts counts_{};
    BayCountsCell counts_cell_{};

    std::optional<hal::RegisterWindow<hal::SysDirectory>> enable_header_{};
    LwHostEnable enable_;
    OcramHostEnable ocram_;

    infra::OptRef<OcramLaneWriter> download_lane_{};
    std::optional<os::UioHandle> download_line_{};
};

}
