// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "hal/thread_map.h"
#include "os/types.h"

namespace mister::hal {

struct IrqPin {
    std::string_view action;
    os::CpuMask cpus;
};

inline constexpr std::size_t kMaxIrqPins = 4;

constexpr bool irq_pins_well_formed(std::span<const IrqPin> pins, const ThreadMap& m) noexcept {
    if (pins.size() > kMaxIrqPins) return false;
    const auto hi = static_cast<unsigned>(max_cpu(m));
    const std::uint32_t known = hi >= 31u ? 0xFFFF'FFFFu : (2u << hi) - 1u;
    const std::uint32_t rt = 1u << static_cast<unsigned>(seat_of(m, SeatTag::RT).cpu);
    for (const IrqPin& p : pins) {
        if (p.action.empty()) return false;
        if (p.cpus.v == 0 || (p.cpus.v & ~known) != 0) return false;
        if ((p.cpus.v & rt) != 0) return false;
    }
    return true;
}

enum class IrqPinOutcome : std::uint8_t {
    Applied,
    NoInterruptsFile,
    ListTruncated,
    NoSuchAction,
    WriteRefused,
    ReadbackUnreadable,
    ReadbackMismatch,
};

struct IrqPinResult {
    std::string_view action;
    IrqPinOutcome outcome = IrqPinOutcome::NoSuchAction;
    os::KernelIrq irq;
    os::CpuMask requested;
    os::CpuMask read_back;
    os::CpuMask effective;
    bool effective_known = false;
    int err = 0;
};

struct IrqPinReport {
    std::array<IrqPinResult, kMaxIrqPins> pins{};
    std::size_t declared = 0;
    std::size_t count = 0;
    bool table_refused = false;
    bool all_applied = false;
    int first_err = 0;
};

inline constexpr const char* kProcRoot = "/proc";

[[nodiscard]] std::optional<os::KernelIrq> irq_of_action(std::string_view interrupts,
                                                         std::string_view action) noexcept;

[[nodiscard]] std::optional<os::CpuMask> parse_cpu_mask(std::string_view text) noexcept;

[[nodiscard]] IrqPinResult pin_irq_affinity(const IrqPin& pin, const char* proc_root) noexcept;

[[nodiscard]] IrqPinReport pin_irq_affinities(std::span<const IrqPin> pins, const ThreadMap& map,
                                              const char* proc_root) noexcept;

}  // namespace mister::hal
