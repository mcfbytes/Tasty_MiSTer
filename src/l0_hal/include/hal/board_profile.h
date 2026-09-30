// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "hal/axi.h"
#include "hal/compatible_blob.h"
#include "hal/doorbell_policy.h"
#include "hal/fact.h"
#include "hal/fact_field.h"
#include "hal/fpga_aperture.h"
#include "hal/irq_pin.h"
#include "hal/kernel_contract.h"
#include "hal/link_timing.h"
#include "hal/memory_model.h"
#include "hal/phys_region.h"
#include "hal/program_geometry.h"
#include "hal/region_id.h"
#include "hal/thread_map.h"
#include "hal/video_out_decl.h"
#include "hal/window_decl.h"
#include "os/types.h"

namespace mister::hal {

enum class BoardId : std::uint8_t { De10Nano, De25Nano, Zynq7000, SpiSbc };
inline constexpr std::size_t kBoardCount = 4;

inline constexpr FpgaAperture kUnsourcedFpgaMem{
    {os::PhysAddr{0u}, regions::kLenUnsourced, "unsourced"}, 0u};

inline constexpr std::array<FpgaAperture, kBoardCount> kBoardApertures{{
    {{kFpgaMemBase, 0x2000'0000u, "fpga-ddr"}, kFpgaMemMask},
    kUnsourcedFpgaMem,
    kUnsourcedFpgaMem,
    kUnsourcedFpgaMem,
}};

struct BoardProfile {
    BoardId id;
    std::string_view model;
    std::span<const std::string_view> compatible;
    std::span<const WindowDecl> windows;

    WindowId mailbox_window;
    Fact<std::uint32_t> mailbox_gpo_offset;
    Fact<std::uint32_t> mailbox_gpi_offset;

    WindowId lw_window;

    FpgaAperture fpga_mem;

    std::span<const PhysRegion> regions;

    std::uint32_t f2h_lines;
    DoorbellPolicy doorbells;

    std::span<const IrqPin> irq_pins;

    VideoOutDecl video;
    KernelContract kernel;

    const ThreadMap& threads;
    int main_cpu;

    bool verified;

    LinkTiming timing;
    MemoryModel memory;
    ProgramGeometry program;
};

constexpr bool fact_measured(const BoardProfile& profile, FactField f) noexcept {
    switch (f) {
        case FactField::MailboxGpoOffset:
            return profile.mailbox_gpo_offset.measured();
        case FactField::MailboxGpiOffset:
            return profile.mailbox_gpi_offset.measured();
        case FactField::VideoI2cBuses:
            return profile.video.i2c_buses.measured();
        case FactField::TimingWordNs:
            return profile.timing.word_ns.measured();
        case FactField::TimingBeatNs:
            return profile.timing.beat_ns.measured();
        case FactField::TimingBaseRoundNs:
            return profile.timing.base_round_ns.measured();
        case FactField::TimingSleepNs:
            return profile.timing.sleep_ns.measured();
        case FactField::TimingRoundBudgetWords:
            return profile.timing.round_budget_words.measured();
        case FactField::TimingAckSoftSpins:
            return profile.timing.ack_soft_spins.measured();
        case FactField::TimingAckTimeoutNs:
            return profile.timing.ack_timeout_ns.measured();
        case FactField::MemoryFpgaDdr:
            return profile.memory.fpga_ddr.measured();
        case FactField::MemoryWriteCombine:
            return profile.memory.write_combine.measured();
        case FactField::ProgramChunkBytes:
            return profile.program.chunk_bytes.measured();
        case FactField::ProgramQuantum:
            return profile.program.quantum.measured();
        case FactField::ProgramAlign:
            return profile.program.align.measured();
        case FactField::TimingLoadBudgetWords:
            return profile.timing.load_budget_words.measured();
    }
    return false;
}

constexpr bool fully_measured(const BoardProfile& profile) noexcept {
    for (std::size_t i = 0; i < kFactFieldCount; ++i) {
        if (!fact_measured(profile, static_cast<FactField>(i))) return false;
    }
    return true;
}

constexpr const WindowDecl& window(const BoardProfile& profile, WindowId id) noexcept {
    return profile.windows[static_cast<std::size_t>(id)];
}

constexpr PhysRegion lw_region(const BoardProfile& profile) noexcept {
    const auto i = static_cast<std::size_t>(profile.lw_window);
    return i < profile.windows.size() ? profile.windows[i].region
                                      : PhysRegion{os::PhysAddr{0u}, 0u, "unsourced-lw"};
}

constexpr const PhysRegion& region(const BoardProfile& profile, RegionId id) noexcept {
    return profile.regions[static_cast<std::size_t>(id)];
}

constexpr os::UioLineSpace doorbell_nodes(const BoardProfile& profile) noexcept {
    return {.node_prefix = profile.kernel.doorbell_prefix, .lines = profile.f2h_lines};
}

inline constexpr const char* kCompatiblePath = "/proc/device-tree/compatible";

[[nodiscard]] Ex<CompatibleBlob> read_compatible(const char* path);

[[nodiscard]] Ex<const BoardProfile*> select_board(const CompatibleBlob& blob);

}  // namespace mister::hal
