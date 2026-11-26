// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "hal/axi.h"
#include "hal/board_profile.h"
#include "hal/fact.h"
#include "hal/irq_pin.h"
#include "hal/thread_map.h"
#include "hal/window_decl.h"

namespace mister::hal {

using std::literals::chrono_literals::operator""ns;

inline constexpr std::array<std::string_view, 3> kDe10Compatible = {
    "terasic,de10-nano",
    "altr,socfpga-cyclone5",
    "altr,socfpga",
};

inline constexpr auto kDe10Windows = std::to_array<WindowDecl>({
    {.id = WindowId{0},
     .region = {os::PhysAddr{0xFF70'6000u}, 0x1000u, "fpga-mgr"},
     .uio_name = "mister_fpga_mgr"},
    {.id = WindowId{1},
     .region = {os::PhysAddr{0xFFB9'0000u}, 0x1000u, "fpga-mgr-data"},
     .uio_name = "mister_fpga_data"},
    {.id = WindowId{2},
     .region = {os::PhysAddr{0xFFC2'0000u}, 0x6000u, "sdr-ctl"},
     .uio_name = "mister_sdr"},
    {.id = WindowId{3},
     .region = {os::PhysAddr{0xFFD0'5000u}, 0x100u, "rstmgr"},
     .uio_name = "mister_rstmgr"},

    {.id = WindowId{4},
     .region = {os::PhysAddr{0xFFD0'8000u}, 0x200u, "sysmgr"},
     .uio_name = "mister_sysmgr"},
    {.id = WindowId{5},
     .region = {os::PhysAddr{0xFF80'0000u}, 0x100u, "nic301"},
     .uio_name = "mister_nic301"},
    {.id = WindowId{6},
     .region = {kLwBridgeBase, kLwBridgeSize, "lw-bridge"},
     .uio_name = "mister_lw_bridge"},
});
static_assert(
    windows_well_formed(kDe10Windows),
    "DE10 window rows must be in position order, one row per id"
);

inline constexpr std::array<PhysRegion, kRegionCount> kDe10Regions{
    regions::kMinimigShare,
    regions::kX86Share,
    regions::kX86Mem,
    regions::kNicMailbox,
    regions::kVideoFb,
    regions::kScalerOut,
};
static_assert(
    regions_well_formed(kDe10Regions),
    "DE10 region rows must be in RegionId enum order, one row per id"
);

consteval bool de10_sources_every_region() {
    for (std::size_t i = 0; i < kRegionCount; ++i) {
        const PhysRegion& want = regions::kCatalog[i];
        const PhysRegion& have = kDe10Regions[i];
        if (have.phys != want.phys || have.len != want.len)
            return false;
    }
    return true;
}

static_assert(
    de10_sources_every_region(),
    "every hal::regions:: constant must appear in the DE10 board row"
);

inline constexpr std::array<IrqPin, 1> kDe10IrqPins{{
    {.action = "ffb40000.usb", .cpus = os::CpuMask{0x1u}},
}};

inline constexpr BoardProfile kDe10Profile{
    .id = BoardId::De10Nano,
    .model = "Terasic DE10-Nano",
    .compatible = kDe10Compatible,
    .windows = kDe10Windows,
    .gpo_gpi_window = window_named(kDe10Windows, "fpga-mgr"),
    .gpo_offset = 0x10u,
    .gpi_offset = 0x14u,
    .lw_window = window_named(kDe10Windows, "lw-bridge"),

    .ocram_window = {os::PhysAddr{0xFFFF'0000u}, 0xF000u, "ocram"},
    .fpga_mem = kBoardApertures[std::to_underlying(BoardId::De10Nano)],
    .regions = kDe10Regions,
    .f2h_lines = 64u,
    .doorbells =
        {.pool = 8u,
         .first_f2h = os::F2hLine{8u},
         .vsync_line = os::F2hLine{0u},
         .cause_base = LwOffset{0x1000u},
         .cause_span = 0x1000u},
    .irq_pins = kDe10IrqPins,
    .video = {.i2c_buses = I2cBusRange{.first = 0u, .last = 2u}, .vsync_device = "/dev/fb0"},

    .kernel =
        {.doorbell_prefix = "mister_f2h_irq",
         .sd_block = "mmcblk0",
         .activity_led = {},
         .ddr_aperture = "mister_ddr_aperture"},
    .threads = kDe10ThreadMap,
    .main_cpu = os::CpuId{0},
    .verified = true,

    .timing =
        {.word_ns = 1'250ns,
         .beat_ns = 3'900ns,
         .base_round_ns = 155'000ns,
         .sleep_ns = 75'000ns,
         .round_budget_words = 460u,
         .load_budget_words = 8'000u,
         .ack_soft_spins = 4'096u,
         .ack_timeout_ns = 10'000'000ns},
    .memory = {.fpga_ddr = DdrCoherency::NonCoherent, .write_combine = WriteCombine::Allowed},
    .program = {
        .chunk_bytes = ChunkBytes{64u * 1'024u},
        .quantum = QuantumBytes{32u},
        .align = AlignBytes{4u}
    },
};

inline constexpr std::array<std::string_view, 2> kDe25Compatible = {
    "intel,socfpga-agilex5-socdk",
    "intel,socfpga-agilex5",
};
inline constexpr std::array<std::string_view, 1> kZynq7000Compatible = {"xlnx,zynq-7000"};
inline constexpr std::array<std::string_view, 1> kSpiSbcCompatible = {"mister,link-spidev"};

static_assert(
    regions_well_formed(regions::kUnsourced),
    "the unsourced region row must still carry one entry per RegionId"
);

consteval BoardProfile placeholder_row(
    BoardId id,
    std::string_view model,
    std::span<const std::string_view> compatible,
    const ThreadMap& threads,
    os::CpuId main_cpu
) {
    return BoardProfile{
        .id = id,
        .model = model,
        .compatible = compatible,
        .windows = {},
        .gpo_gpi_window = WindowId{},
        .gpo_offset = kUnmeasured,
        .gpi_offset = kUnmeasured,
        .lw_window = WindowId{},
        .ocram_window = {os::PhysAddr{0u}, 0u, "unsourced-ocram"},
        .fpga_mem = kBoardApertures[std::to_underlying(id)],
        .regions = regions::kUnsourced,
        .f2h_lines = 0u,
        .doorbells = {},
        .irq_pins = {},
        .video = {.i2c_buses = kUnmeasured, .vsync_device = nullptr},
        .kernel = {},
        .threads = threads,
        .main_cpu = main_cpu,
        .verified = false,
        .timing =
            {.word_ns = kUnmeasured,
             .beat_ns = kUnmeasured,
             .base_round_ns = kUnmeasured,
             .sleep_ns = kUnmeasured,
             .round_budget_words = kUnmeasured,
             .load_budget_words = kUnmeasured,
             .ack_soft_spins = kUnmeasured,
             .ack_timeout_ns = kUnmeasured},
        .memory = {.fpga_ddr = kUnmeasured, .write_combine = kUnmeasured},
        .program = {.chunk_bytes = kUnmeasured, .quantum = kUnmeasured, .align = kUnmeasured},
    };
}

inline constexpr BoardProfile kDe25Profile = placeholder_row(
    BoardId::De25Nano,
    "Terasic DE25-Nano",
    kDe25Compatible,
    kDe25ThreadMap,
    os::CpuId{0}
);
inline constexpr BoardProfile kZynq7000Profile = placeholder_row(
    BoardId::Zynq7000,
    "Xilinx Zynq-7000",
    kZynq7000Compatible,
    kDe10ThreadMap,
    os::CpuId{0}
);
inline constexpr BoardProfile kSpiSbcProfile = placeholder_row(
    BoardId::SpiSbc,
    "SBC + FPGA over spidev (MiSTeX shape)",
    kSpiSbcCompatible,
    kDe10ThreadMap,
    os::CpuId{0}
);

inline constexpr std::array<const BoardProfile*, kBoardCount> kBoardTable{
    &kDe10Profile,
    &kDe25Profile,
    &kZynq7000Profile,
    &kSpiSbcProfile,
};

[[nodiscard]] constexpr std::span<const BoardProfile* const> board_table() noexcept {
    return kBoardTable;
}

[[nodiscard]] constexpr const BoardProfile& board_by_id(BoardId id) noexcept {
    return *kBoardTable[static_cast<std::size_t>(id)];
}

consteval bool boards_claim_disjoint_compatible_strings() {
    for (std::size_t i = 0; i < kBoardTable.size(); ++i) {
        for (const std::string_view si : kBoardTable[i]->compatible) {
            for (std::size_t j = i + 1; j < kBoardTable.size(); ++j) {
                for (const std::string_view sj : kBoardTable[j]->compatible) {
                    if (si == sj)
                        return false;
                }
            }
        }
    }
    return true;
}

static_assert(
    boards_claim_disjoint_compatible_strings(),
    "two board rows must never claim the same compatible string"
);

consteval bool boards_in_id_order() {
    for (std::size_t i = 0; i < kBoardTable.size(); ++i) {
        if (static_cast<std::size_t>(kBoardTable[i]->id) != i)
            return false;
    }
    return true;
}

static_assert(boards_in_id_order(), "board rows must be in BoardId enum order");

consteval bool boards_carry_the_published_aperture() {
    for (std::size_t i = 0; i < kBoardTable.size(); ++i) {
        const FpgaAperture& have = kBoardTable[i]->fpga_mem;
        const FpgaAperture& want = kBoardApertures[i];
        if (have.region.phys != want.region.phys
            || have.region.len != want.region.len
            || have.mask != want.mask) {
            return false;
        }
    }
    return true;
}

static_assert(
    boards_carry_the_published_aperture(),
    "every board row's fpga_mem must be its kBoardApertures entry"
);

consteval bool boards_regions_are_on_their_side() {
    for (const BoardProfile* p : kBoardTable) {
        const FpgaAperture& ap = p->fpga_mem;
        for (std::size_t i = 0; i < kRegionCount; ++i) {
            const PhysRegion& r = p->regions[i];
            if (r.len == regions::kLenUnsourced)
                continue;
            if (ap.region.len == 0)
                return false;
            if (regions::kSide[i] == ApertureSide::Below) {
                if (r.end() > ap.region.phys)
                    return false;
            } else if (
                r.phys < ap.region.phys
                || !span_fits<std::uint64_t>(
                    r.phys.offset_from(ap.region.phys),
                    r.len,
                    ap.region.len
                )
            ) {
                return false;
            }
        }
    }
    return true;
}

static_assert(
    boards_regions_are_on_their_side(),
    "a SOURCED board region must lie on the side kSide declares: Inside wholly "
    "inside the board's aperture, Below ending at or below its base"
);

consteval bool boards_irq_rows_well_formed() {
    for (const BoardProfile* p : kBoardTable) {
        if (!irq_pins_well_formed(p->irq_pins, p->threads))
            return false;
    }
    return true;
}

static_assert(
    boards_irq_rows_well_formed(),
    "an IRQ row must name an action, stay within its board's CPUs, number at most "
    "kMaxIrqPins and keep the line OFF T-RT's core — the only reason a row exists"
);

consteval bool boards_window_roles_well_formed() {
    for (const BoardProfile* p : kBoardTable) {
        if (p->windows.empty())
            continue;
        const auto mbox = static_cast<std::size_t>(p->gpo_gpi_window);
        const auto lw = static_cast<std::size_t>(p->lw_window);
        if (mbox >= p->windows.size() || lw >= p->windows.size() || mbox == lw)
            return false;
    }
    return true;
}

static_assert(
    boards_window_roles_well_formed(),
    "a row with windows must name distinct, in-range mailbox and LW windows"
);

consteval bool sources_no_fabric(const BoardProfile& p) {
    if (!p.windows.empty() || p.f2h_lines != 0u || p.ocram_window.len != 0u)
        return false;
    if (p.doorbells.cause_span != 0u)
        return false;
    if (p.fpga_mem.region.len != regions::kLenUnsourced)
        return false;
    for (const PhysRegion& r : p.regions) {
        if (r.len != regions::kLenUnsourced)
            return false;
    }
    return p.kernel.doorbell_prefix.empty() && p.kernel.ddr_aperture.empty();
}

consteval bool unverified_rows_source_no_fabric() {
    for (const BoardProfile* p : kBoardTable) {
        if (!p->verified && !sources_no_fabric(*p))
            return false;
    }
    return true;
}

static_assert(
    unverified_rows_source_no_fabric(),
    "an unverified board row may source no fabric address and name no fabric node"
);
static_assert(sources_no_fabric(kDe25Profile) && !sources_no_fabric(kDe10Profile));

consteval bool verified_boards_are_fully_measured() {
    for (const BoardProfile* p : kBoardTable) {
        if (p->verified && !fully_measured(*p))
            return false;
    }
    return true;
}

static_assert(
    verified_boards_are_fully_measured(),
    "a verified board row may carry no Unmeasured fact"
);

template <class Pred>
consteval bool every_board(Pred pred) {
    for (const BoardProfile* p : board_table()) {
        if (!pred(*p))
            return false;
    }
    return true;
}

template <class Group, class Pred>
consteval bool every_measured(Group BoardProfile::* group, Pred pred) {
    return every_board([&](const BoardProfile& b) {
        return !all_measured(b.*group) || pred(b.*group);
    });
}

template <class Pred>
consteval bool every_thread_map(Pred pred) {
    return every_board([&](const BoardProfile& b) { return pred(b.threads); });
}

consteval bool boards_place_main_off_rt_cpu() {
    return every_board([](const BoardProfile& b) {
        return main_off_rt_cpu(b.threads, b.main_cpu);
    });
}

static_assert(
    every_thread_map([](const ThreadMap& m) { return well_formed(m); }),
    "Every board's thread-map rows must be in SeatTag order, "
    "one row per real seat. seat_of() indexes by row_index and the "
    "evidence record does too; a reordered or duplicated row "
    "would silently hand a call site the wrong seat's policy, "
    "priority and CPU."
);

static_assert(
    every_thread_map([](const ThreadMap& m) { return carries_seat_table(m); }),
    "Every board's map is the seat table joined to that board's placement "
    "(join_threads): its policy, priority, stack, spawn, DDR and pause columns are "
    "kSeatPolicyTable's, so the seat table's rules hold on it. A board states its CPUs "
    "and nothing else; a seat rule changes in thread_map.h, never in one board's map."
);

static_assert(
    every_thread_map([](const ThreadMap& m) { return rt_cpu_reserved(m); }),
    "Only the kRtCpuSeats seats (T-RT, T-FRAME) are pinned to T-RT's "
    "CPU, at any policy. Every other seat does blocking I/O, payload-sized copies or "
    "SCHED_OTHER work, and on that core it contends with the round the executive's 5 ms "
    "cadence is measured on; a SCHED_OTHER thread there is unthrottled contention."
);

static_assert(
    boards_place_main_off_rt_cpu(),
    "MAIN (the process main thread, SCHED_OTHER) must not sit on T-RT's CPU: "
    "that core is the SPI and RT round's alone, and a SCHED_OTHER thread there "
    "is contention the round is measured against. main_cpu must also be a CPU some row "
    "already names, so both arms of fw::rt_topology_init cover it."
);

static_assert(
    max_cpu(kDe25Profile.threads) == os::CpuId{3}
        && seat_of(kDe25Profile.threads, SeatTag::RT).cpu == os::CpuId{1},
    "The DE25-Nano row's map is 2x Cortex-A55 (CPUs 0-1) + 2x Cortex-A76 (CPUs 2-3), "
    "with T-RT on A55 CPU 1 until rtstats says otherwise."
);

static_assert(
    max_cpu(kDe10Profile.threads) == os::CpuId{1},
    "The DE10-Nano row's map is 2x Cortex-A9. A row naming "
    "CPU2+ belongs to another board, which gets its own placement in its "
    "own row, never an edit to this one; re-read the runtime sysconf "
    "gate in fw::rt_topology_init before that happens."
);

static_assert(
    every_thread_map([](const ThreadMap& m) {
        for (const ThreadRole& r : m)
            if (!has_row(r.seat))
                return false;
        return true;
    }),
    "no ThreadMap row is Unbound or Main: an Unbound thread passes no TASTY_SEAT "
    "check, ::main places MAIN on main_cpu, and seat_of() has a row for neither."
);

consteval bool boards_ack_bounds_nonzero() {
    return every_measured(&BoardProfile::timing, [](const LinkTiming& t) consteval {
        return t.ack_soft_spins.value() != 0u
            && t.ack_timeout_ns.value() != std::chrono::nanoseconds::zero();
    });
}

static_assert(
    boards_ack_bounds_nonzero(),
    "a measured row's ACK spin bounds must be nonzero, and the start bound never "
    "shorter than the steady-state one"
);

}
