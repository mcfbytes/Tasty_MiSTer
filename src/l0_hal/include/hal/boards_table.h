// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
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
     .region = {os::PhysAddr{0xFFD0'8000u}, 0x100u, "sysmgr"},
     .uio_name = "mister_sysmgr"},
    {.id = WindowId{5},
     .region = {os::PhysAddr{0xFF80'0000u}, 0x100u, "nic301"},
     .uio_name = "mister_nic301"},
    {.id = WindowId{6},
     .region = {kLwBridgeBase, kLwBridgeSize, "lw-bridge"},
     .uio_name = "mister_lw_window"},
});
static_assert(
    windows_well_formed(kDe10Windows),
    "DE10 window rows must be in position order, one row per id"
);

inline constexpr std::array<PhysRegion, kRegionCount> kDe10Regions{
    regions::kMinimigShare,
    regions::kX86Share,
    regions::kX86Mem,
    regions::kA2065Flat,
    regions::kSaturnCdBuf,
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
        if (have.phys.v != want.phys.v || have.len != want.len)
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
    .mailbox_window = window_named(kDe10Windows, "fpga-mgr"),
    .mailbox_gpo_offset = 0x10u,
    .mailbox_gpi_offset = 0x14u,
    .lw_window = window_named(kDe10Windows, "lw-bridge"),
    .fpga_mem = kBoardApertures[std::to_underlying(BoardId::De10Nano)],
    .regions = kDe10Regions,
    .f2h_lines = 64u,
    .doorbells = {.pool = 8u, .cause_base = LwOffset{0x1000u}, .cause_span = 0x1000u},
    .irq_pins = kDe10IrqPins,
    .video = {.i2c_buses = I2cBusRange{.first = 0u, .last = 2u}, .vsync_device = "/dev/fb0"},
    .kernel = {.doorbell_prefix = "mister_doorbell", .sd_block = "mmcblk0"},
    .threads = kDe10ThreadMap,
    .main_cpu = 0,
    .verified = true,

    .timing =
        {.word_ns = 1'250u,
         .beat_ns = 3'900u,
         .base_round_ns = 155'000u,
         .sleep_ns = 75'000u,
         .round_budget_words = 460u,
         .load_budget_words = 8'000u,
         .ack_soft_spins = 4'096u,
         .ack_timeout_ns = std::uint64_t{10'000'000u},
         .start_ack_timeout_ns = std::uint64_t{250'000'000u}},
    .memory = {.fpga_ddr = DdrCoherency::NonCoherent, .write_combine = WriteCombine::Allowed},
    .program = {.chunk_bytes = 64u * 1'024u, .quantum = 32u, .align = 4u},
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
    std::span<const std::string_view> compatible
) {
    return BoardProfile{
        .id = id,
        .model = model,
        .compatible = compatible,
        .windows = {},
        .mailbox_window = WindowId{},
        .mailbox_gpo_offset = kUnmeasured,
        .mailbox_gpi_offset = kUnmeasured,
        .lw_window = WindowId{},
        .fpga_mem = kBoardApertures[std::to_underlying(id)],
        .regions = regions::kUnsourced,
        .f2h_lines = 0u,
        .doorbells = {},
        .irq_pins = {},
        .video = {.i2c_buses = kUnmeasured, .vsync_device = nullptr},
        .kernel = {},
        .threads = kDe10ThreadMap,
        .main_cpu = 0,
        .verified = false,
        .timing =
            {.word_ns = kUnmeasured,
             .beat_ns = kUnmeasured,
             .base_round_ns = kUnmeasured,
             .sleep_ns = kUnmeasured,
             .round_budget_words = kUnmeasured,
             .load_budget_words = kUnmeasured,
             .ack_soft_spins = kUnmeasured,
             .ack_timeout_ns = kUnmeasured,
             .start_ack_timeout_ns = kUnmeasured},
        .memory = {.fpga_ddr = kUnmeasured, .write_combine = kUnmeasured},
        .program = {.chunk_bytes = kUnmeasured, .quantum = kUnmeasured, .align = kUnmeasured},
    };
}

inline constexpr BoardProfile kDe25Profile = placeholder_row(
    BoardId::De25Nano,
    "SoCFPGA Agilex5 Terasic DE25-Nano",
    kDe25Compatible
);
inline constexpr BoardProfile kZynq7000Profile = placeholder_row(
    BoardId::Zynq7000,
    "Xilinx Zynq-7000",
    kZynq7000Compatible
);
inline constexpr BoardProfile kSpiSbcProfile = placeholder_row(
    BoardId::SpiSbc,
    "SBC + FPGA over spidev (MiSTeX shape)",
    kSpiSbcCompatible
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
        if (have.region.phys.v != want.region.phys.v
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
        const auto mbox = static_cast<std::size_t>(p->mailbox_window);
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
    "item: every board's thread-map rows must be in SeatTag order, "
    "one row per real seat. seat_of() indexes by row_index and the "
    "evidence record does too; a reordered or duplicated row "
    "would silently hand a call site the wrong seat's policy, "
    "priority and CPU."
);

static_assert(
    every_thread_map([](const ThreadMap& m) { return rt_holds_highest_fifo_prio(m); }),
    "arch §2 / item INVARIANT 1: T-RT must hold the strictly "
    "highest SCHED_FIFO priority of any seat. If you raised another "
    "seat to or above T-RT, that seat can now preempt the sole GPO "
    "owner mid-SPI-transaction — the gpo_copy shadow hazard by its "
    "real name (arch amendment 2026-08-18, RULE 2)."
);

static_assert(
    every_thread_map([](const ThreadMap& m) { return fifo_order_holds(m); }),
    "item INVARIANTS 3 and 4: exactly the kFifoOrder seats are SCHED_FIFO, at strictly "
    "falling priority (T-RT, T-FRAME, T-INPUT, T-IO, T-PCM, T-CAPTURE, T-PREFETCH), and "
    "every other seat is SCHED_OTHER. Deadline-monotonic: each edge is a deadline, and "
    "inverting one queues the shorter deadline behind the longer. A tie is the "
    "kernel's run-queue accident; a seat moved to SCHED_OTHER loses the bounded wake it "
    "exists to buy. Change the order and the row together, never the number alone."
);

static_assert(
    every_thread_map([](const ThreadMap& m) { return rt_cpu_reserved(m); }),
    "item INVARIANT 2: only the kRtCpuSeats seats (T-RT, T-FRAME) are pinned to T-RT's "
    "CPU, at any policy. Every other seat does blocking I/O, payload-sized copies or "
    "SCHED_OTHER work, and on that core it contends with the round the executive's 5 ms "
    "cadence is measured on; a SCHED_OTHER thread there is unthrottled contention."
);

static_assert(
    every_thread_map([](const ThreadMap& m) { return ddr_owners_are(m); }),
    "every board's DdrMapping::Owner rows are exactly kDdrMappingOwners: a late owner "
    "refuses the program, so an unmarked seat holding a DDR mapping would be programmed "
    "under, and a marked seat that never parks would wedge every switch."
);

static_assert(
    every_thread_map([](const ThreadMap& m) { return policy_prio_consistent(m); }),
    "item / item: `policy` and `prio` are ONE decision. A "
    "SchedPolicy::Fifo row must carry a POSIX priority in 1..99 and "
    "a SchedPolicy::Other row must carry 0. Both `{Fifo, 0}` and "
    "`{Fifo, 120}` used to compile and then fail at runtime as an "
    "EINVAL from pthread_attr_setschedparam, taking spawn_seat's "
    "rung-2 degrade with no witness but an evidence row nobody "
    "reads. If you meant to move a seat off SCHED_FIFO, change BOTH "
    "fields and re-read INVARIANT 3 first."
);

static_assert(
    every_thread_map([](const ThreadMap& m) { return spawn_stack_consistent(m); }),
    "pass ST / item: `spawn` and `stack_bytes` are ONE decision. A "
    "SpawnKind::Create row with stack_bytes == 0 is an EINVAL from "
    "pthread_attr_setstacksize that spawn_seat's rung-2 degrade "
    "swallows with no witness (the attr is discarded and the seat is "
    "born on glibc's default stack, unpinned by this table); a "
    "PromoteInPlace row with a stack owns memory nothing allocates. "
    "Change BOTH fields or neither."
);

static_assert(
    every_thread_map([](const ThreadMap& m) {
        return seat_of(m, SeatTag::RT).spawn == SpawnKind::Create;
    }),
    "pass ST (c): T-RT is a CREATED thread on its own row-declared "
    "stack, spawned LAST, and ::main is the supervisor that joins it "
    "under a deadline. A PromoteInPlace T-RT IS main, and a thread "
    "cannot time-join itself — a wedged executive would then have no "
    "witness but a silent hang."
);

static_assert(
    names_fit_comm(kSeatNames),
    "item: every seat name must be non-empty and <= 15 chars, "
    "because pthread_setname_np takes 16 bytes INCLUDING the NUL "
    "(glibc returns ERANGE; the kernel truncates). A name that does "
    "not survive is a /proc witness that no longer matches this "
    "table, which is the only channel RT engagement is verified on "
    "(arch §7 retraction 7)."
);

static_assert(
    boards_place_main_off_rt_cpu(),
    "MAIN (the process main thread, SCHED_OTHER) must not sit on T-RT's CPU: "
    "that core is the SPI and RT round's alone, and a SCHED_OTHER thread there "
    "is contention the round is measured against. main_cpu must also be a CPU some row "
    "already names, so both arms of fw::rt_topology_init cover it."
);

static_assert(
    max_cpu(kDe10Profile.threads) == 1,
    "item: the DE10-Nano row's map is 2x Cortex-A9. A row naming "
    "CPU2+ belongs to another board, which gets its own table in its "
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
            && t.ack_timeout_ns.value() != 0u
            && t.start_ack_timeout_ns.value() >= t.ack_timeout_ns.value();
    });
}

static_assert(
    boards_ack_bounds_nonzero(),
    "a measured row's ACK spin bounds must be nonzero, and the start bound never "
    "shorter than the steady-state one"
);

}
