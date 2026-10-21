// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/config_slots.h"
#include "cores/st_config_layout.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"

namespace mister::cores {

struct StConfig {
    TASTY_SEAT_EXEMPT(component);

    static constexpr std::size_t kAcsiTargets = 2;
    using Path = FixedStr<st_cfg::kPathBytes, StrFit::Clip>;

    std::uint32_t system_ctrl = 0;
    std::uint32_t ext_ctrl = 0;
    Path tos_img{};
    std::array<Path, kAcsiTargets> acsi_img{};

    [[nodiscard]] static StConfig defaults() noexcept;

    void overlay(
        std::span<const std::byte> blob,
        std::string_view cart,
        std::span<std::byte, st_cfg::kBytes> img
    ) noexcept;

    void encode(std::span<std::byte, st_cfg::kBytes> img, std::string_view cart) const noexcept;
    void decode(std::span<const std::byte, st_cfg::kBytes> img) noexcept;

    [[nodiscard]] static std::string_view cart_of(
        std::span<const std::byte, st_cfg::kBytes> img
    ) noexcept;
    [[nodiscard]] static FixedStr<kConfigPreviewChars, StrFit::Clip> preview(
        ConfigBlob blob
    ) noexcept;
    [[nodiscard]] static std::string_view leaf_of(std::string_view path) noexcept;
};

}
