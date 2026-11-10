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
#include "infra/indexed_array.h"
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

class BindTail;

class CoreBuilder {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct Ask {
        std::string_view name;
        cores::CoreManifest manifest{};
        std::span<const proto::IrqBinding> irqs{};
        std::span<const proto::AxiLwDecl> lw{};

        bool fabric = false;
        FabricGeneration fabric_gen{};
        proto::BlockSlotMask mount_slots{};
    };

    struct SectorCells {
        SectorGrantCell& grant;
        const SectorRouteCell& route;
        infra::OptRef<xthread::WakeFlag> wake{};
    };

    struct OsdCells {
        OsdGrantCell& grant;
        const OsdRouteCell& route;
    };

    struct SysCells {
        SysGrantCell& grant;
        const SysRouteCell& route;
        infra::OptRef<xthread::WakeFlag> wake{};
    };

    struct Wiring {
        CoreBay& bay;
        const BayReleasesCell& releases;
        const CoreTerms& terms;
        std::optional<SectorCells> sectors{};
        std::optional<OsdCells> osds{};
        std::optional<SysCells> sys{};
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

    [[nodiscard]] static DoorbellLines open_doorbells(
        hal::IDoorbellSource& src,
        const hal::DoorbellPolicy& policy,
        std::span<const proto::IrqBinding> irqs
    ) noexcept;

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
        const Directory& dir,
        const proto::AxiLwDecl& decl
    ) noexcept;

    [[nodiscard]] const std::optional<proto::LaneBlockDecl>& lane() const noexcept { return lane_; }

    [[nodiscard]] std::uint32_t directory_reads() const noexcept { return directory_reads_; }

    [[nodiscard]] static bool card_admitted(
        const cores::CoreProfile& profile,
        const proto::SectorBlockDecl::Card& card,
        proto::SlotIndex block_slot
    ) noexcept;
    void count_lease_hold(bool long_hold) noexcept;

    [[nodiscard]] static bool fabric_owned(
        const proto::IrqBinding& b,
        std::span<const proto::AxiLwDecl> lw
    ) noexcept;

    struct RtRows {

        struct RtRowCount {
            std::size_t v = 0;

            [[nodiscard]] constexpr RtRowCount next() const noexcept { return RtRowCount{v + 1}; }

            [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

            [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

            template <class T>
            [[nodiscard]] constexpr std::size_t bytes_of() const noexcept {
                return v * sizeof(T);
            }

            template <class T, std::size_t N>
            [[nodiscard]] constexpr std::span<const T> prefix_of(
                const std::array<T, N>& rows
            ) const noexcept {
                return std::span<const T>(rows).first(v);
            }

            template <class T, std::size_t N>
            [[nodiscard]] constexpr std::span<const T> prefix_of(
                const T (&rows)[N]
            ) const noexcept {
                return std::span<const T>(rows).first(v);
            }

            friend constexpr bool operator==(RtRowCount, RtRowCount) = default;

        private:
            friend class BindTail;

            [[nodiscard]] constexpr std::uint8_t wire_byte() const noexcept {
                return static_cast<std::uint8_t>(
                    std::min(v, static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()))
                );
            }
        };

        std::array<proto::IrqBinding, kMaxDoorbellRows> rows{};
        RtRowCount count{};
        RtRowCount declared{};

        [[nodiscard]] std::span<const proto::IrqBinding> table() const noexcept {
            return count.prefix_of(rows);
        }
    };

    [[nodiscard]] static RtRows rt_rows(
        std::span<const proto::IrqBinding> irqs,
        std::span<const proto::AxiLwDecl> lw,
        std::optional<hal::LwOffset> owned
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
        proto::BlockSlotMask declared,
        proto::BlockSlotMask lw_slots
    ) noexcept {
        return !lw_slots.empty() && declared.minus(lw_slots).empty() ? BlockPollRoute::Retired
                                                                     : BlockPollRoute::Link;
    }

    [[nodiscard]] static proto::BlockSlotMask profile_slot_mask(
        const cores::CoreProfile& profile
    ) noexcept;

    [[nodiscard]] static bool fabric_route(std::string_view name, bool manifest) noexcept;

    void raise_sector() noexcept;

    [[nodiscard]] bool retire_sector(bool fabric_ok) noexcept;

    void abandon_sector() noexcept;

    [[nodiscard]] bool sector_mapped() const noexcept { return sector_.has_value(); }

    [[nodiscard]] bool sector_abandoned() const noexcept { return sector_ && sector_->fabric_lost; }

    [[nodiscard]] bool bridge_refused() noexcept;

    [[nodiscard]] static Ex<LwOsd> grant_osd(
        const Directory& dir,
        const proto::AxiLwDecl& decl
    ) noexcept;

    void raise_osd() noexcept;

    [[nodiscard]] bool retire_osd(bool fabric_ok) noexcept;

    void abandon_osd() noexcept;

    [[nodiscard]] bool osd_mapped() const noexcept { return osd_.has_value(); }

    [[nodiscard]] bool osd_abandoned() const noexcept { return osd_ && osd_->fabric_lost; }

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

    [[nodiscard]] const BayCounts& counts() const noexcept { return counts_; }

    [[nodiscard]] const BayCountsCell& counts_cell() const noexcept { return counts_cell_; }

private:
    CoreBuilder(Wiring wiring, infra::OptRef<hal::IDoorbellSource> seam) noexcept;

    [[nodiscard]] hal::IDoorbellSource& source_() noexcept;

    void drop_windows(const ParkReceipt& receipt) noexcept;
    void free_(proto::CoreBaySlot slot) noexcept;
    void publish_counts_() noexcept;
    [[nodiscard]] Ex<Directory> directory_(const proto::AxiLwDecl& decl) noexcept;
    [[nodiscard]] std::optional<proto::LaneBlockDecl> find_lane_(
        std::span<const proto::AxiLwDecl> lw
    ) noexcept;

    [[nodiscard]] std::optional<LwPads> grant_pads_(std::span<const proto::AxiLwDecl> lw) noexcept;

    void grant_sector_(
        std::span<const proto::IrqBinding> irqs,
        std::span<const proto::AxiLwDecl> lw,
        const cores::CoreProfile& profile,
        FabricGeneration fabric_gen
    ) noexcept;

    [[nodiscard]] std::array<LwSector::Card, proto::SectorBlockDecl::kCards> grant_cards_(
        const proto::SectorBlockDecl& block,
        const cores::CoreProfile& profile
    ) noexcept;
    [[nodiscard]] Ex<hal::PhysRegion> card_region_(
        const proto::SectorBlockDecl::Card& card
    ) const noexcept;

    void drop_unbuilt_sector_() noexcept;
    [[nodiscard]] std::optional<hal::LwOffset> unbuilt_cause_() const noexcept;
    [[nodiscard]] bool sector_leased_() const noexcept;
    void revoke_sector_() noexcept;
    void grant_osd_(
        std::span<const proto::AxiLwDecl> lw,
        const cores::CoreProfile& profile,
        FabricGeneration fabric_gen,
        std::uint16_t gen
    ) noexcept;
    [[nodiscard]] bool osd_leased_() const noexcept;
    void revoke_osd_() noexcept;
    void grant_sys_(
        std::span<const proto::IrqBinding> irqs,
        std::span<const proto::AxiLwDecl> lw,
        FabricGeneration fabric_gen,
        std::uint16_t gen
    ) noexcept;

    static constexpr std::size_t kNodeNameCap = 48;

    void pin_sys_line_(os::UioLine line) noexcept;
    [[nodiscard]] bool sys_leased_() const noexcept;
    void revoke_sys_() noexcept;

    CoreBay& bay_;
    const BayReleasesCell& releases_;
    const CoreTerms& terms_;

    infra::OptRef<hal::IDoorbellSource> seam_{};

    hal::UioDoorbellSource uio_doorbells_{};
    BayReleasesCell::Reader released_{};
    BayReleases levels_{};
    infra::IndexedArray<CoreBay::Lease, kCoreBaySlots, proto::CoreBaySlot> held_{};
    std::uint16_t gen_ = 0;
    bool claimed_ = false;
    std::optional<SectorCells> sectors_{};
    std::optional<LwSector> sector_{};
    bool sector_revoked_ = true;

    std::uint16_t lent_gen_ = 0;
    proto::BlockSlotMask lent_cards_{};
    std::uint16_t judged_gen_ = 0;
    std::optional<OsdCells> osds_{};
    std::optional<LwOsd> osd_{};
    bool osd_revoked_ = true;
    std::optional<SysCells> sys_cells_{};
    std::optional<LwSys> sys_{};
    bool sys_revoked_ = true;
    std::uint16_t lent_sys_gen_ = 0;
    std::uint16_t judged_sys_gen_ = 0;
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
    std::optional<proto::LaneBlockDecl> lane_{};

    BayCounts counts_{};
    BayCountsCell counts_cell_{};
};

}
