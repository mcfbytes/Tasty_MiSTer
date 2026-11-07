// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/boot_handoff.h"
#include "hal/fpga_memory.h"
#include "hal/phys_region.h"
#include "os/types.h"

namespace mister::boards::cyclone_v {

inline constexpr hal::PhysRegion kHandoffPage{os::PhysAddr{0x1FFF'F000u}, 0x1000u, "uboot-handoff"};

inline constexpr std::uint8_t kHandoffEnvCookie[4] = {0x21, 0x43, 0x65, 0x87};
inline constexpr std::uint8_t kHandoffSdramCookie[2] = {0x12, 0x57};
inline constexpr std::uint8_t kHandoffAltCfgCookie[3] = {0x34, 0x99, 0xBA};

inline constexpr std::uint32_t kHandoffRebootWarm = 0xBEEF'B001u;
inline constexpr std::size_t kHandoffSdramOffset = 0xF00;
inline constexpr std::size_t kHandoffAltCfgOffset = 0xF04;
inline constexpr std::size_t kHandoffRebootOffset = 0xF08;

inline constexpr std::size_t kHandoffEnvLimit = kHandoffSdramOffset;

inline constexpr std::uint16_t kHandoffSdramPresent = 0x8000;

class UbootHandoffPage final : public hal::IBootHandoff {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct HandoffCell {

        static constexpr std::size_t kSdramValueAt = 2;

        static constexpr std::size_t kCookieTail = 2;
        static constexpr std::size_t kAltCfgValue = 3;

        static constexpr std::size_t kEnvTrailerBytes = sizeof("\"\n") - 1;
    };

    explicit UbootHandoffPage(std::optional<hal::FpgaMemory> page) noexcept
        : page_(std::move(page)) {}

    [[nodiscard]] Ex<hal::HandoffPage> handoff() override;

    [[nodiscard]] Ex<void> write_env(
        std::string_view core_name,
        std::span<const std::byte> cfg
    ) override;

    [[nodiscard]] Ex<void> write_sdram_cfg(std::uint16_t cfg) override;

    [[nodiscard]] Ex<void> write_altcfg(std::uint8_t alt) override;

    [[nodiscard]] Ex<void> write_reboot_flag(bool warm) override;

private:
    [[nodiscard]] std::byte* base() noexcept;

    std::optional<hal::FpgaMemory> page_;
};

}
