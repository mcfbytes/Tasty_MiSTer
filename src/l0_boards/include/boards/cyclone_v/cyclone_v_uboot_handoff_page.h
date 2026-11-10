// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
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

inline constexpr auto kHandoffEnvCookie = std::to_array<std::uint8_t>({0x21, 0x43, 0x65, 0x87});
inline constexpr auto kHandoffSdramCookie = std::to_array<std::uint8_t>({0x12, 0x57});
inline constexpr auto kHandoffAltCfgCookie = std::to_array<std::uint8_t>({0x34, 0x99, 0xBA});

inline constexpr std::uint32_t kHandoffRebootWarm = hal::RebootFlag::kWarm;
inline constexpr std::size_t kHandoffEnvOffset = 0;
inline constexpr std::size_t kHandoffSdramOffset = 0xF00;
inline constexpr std::size_t kHandoffAltCfgOffset = 0xF04;
inline constexpr std::size_t kHandoffRebootOffset = 0xF08;

struct HandoffCell {
    std::size_t at;
    std::span<const std::uint8_t> cookie;

    [[nodiscard]] constexpr bool present(std::span<const std::uint8_t> page) const noexcept {
        return at + cookie.size() <= page.size()
            && std::ranges::equal(cookie, page.subspan(at, cookie.size()));
    }

    constexpr void stamp(std::span<std::uint8_t> page) const noexcept {
        std::ranges::copy(cookie, page.subspan(at, cookie.size()).begin());
    }

    [[nodiscard]] constexpr std::span<const std::uint8_t> value_of(
        std::span<const std::uint8_t> page
    ) const noexcept {
        return page.subspan(at + cookie.size());
    }

    [[nodiscard]] constexpr std::span<std::uint8_t> value_of(
        std::span<std::uint8_t> page
    ) const noexcept {
        return page.subspan(at + cookie.size());
    }
};

inline constexpr HandoffCell kEnvCell{kHandoffEnvOffset, kHandoffEnvCookie};
inline constexpr HandoffCell kSdramCell{kHandoffSdramOffset, kHandoffSdramCookie};
inline constexpr HandoffCell kAltCfgCell{kHandoffAltCfgOffset, kHandoffAltCfgCookie};

inline constexpr std::array<char, 2> kEnvTrailer{'"', '\n'};

inline constexpr std::size_t kHandoffEnvLimit = kHandoffSdramOffset;

inline constexpr std::uint16_t kHandoffSdramPresent = hal::SdramCfg::kCookiePresent;

class UbootHandoffPage final : public hal::IBootHandoff {
    TASTY_SEAT_RESIDENT(Main);

public:
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
