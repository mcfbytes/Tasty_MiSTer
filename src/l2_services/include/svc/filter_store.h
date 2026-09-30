// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "svc/video_service.h"

namespace mister::svc {

class Vfs;
struct FilterBank;
struct FilterSet;
struct FilterSendPlan;

struct FilterPhase {
    std::array<std::int16_t, 4> t{};
    friend constexpr bool operator==(const FilterPhase&, const FilterPhase&) = default;
};

[[nodiscard]] bool same_digest(const FilterBank& a, const FilterBank& b) noexcept;

[[nodiscard]] bool parse_video_filter(std::string_view text, FilterBank& out);

[[nodiscard]] FilterBank nearest_neighbour_bank();

inline constexpr std::size_t kScalerSlots = 4;
inline constexpr std::size_t kScalerNameCap = 1023;
inline constexpr std::size_t kScalerCfgBytes = kScalerSlots * (1 + kScalerNameCap);

struct ScalerSlot {
    std::uint8_t mode = 0;
    std::array<char, kScalerNameCap> name{};
    [[nodiscard]] std::string_view name_view() const noexcept;
};

struct ScalerSeeds {
    std::string_view horz;
    std::string_view vert;
    std::string_view scan;
    std::string_view ilace;
};

[[nodiscard]] std::array<ScalerSlot, kScalerSlots> decode_scaler_cfg(
    std::span<const std::byte> blob, const ScalerSeeds& seeds);

void load_filter_set(const Vfs& vfs, std::string_view core, const ScalerSeeds& seeds,
                     FilterSet& out);

[[nodiscard]] constexpr std::size_t filter_bank_word_count(std::uint16_t ver) noexcept {
    switch (ver & 0x3u) {
        case 1:
            return (kFilterPhases / 16) * kFilterTaps;
        case 2:
        case 3: {
            const std::size_t skip = (ver & 0x4u) != 0 ? 1u : 4u;
            return (kFilterPhases / skip) * kFilterTaps * 2u;
        }
        default:
            return 0;
    }
}

[[nodiscard]] std::uint16_t filter_bank_word(std::span<const FilterPhase> phases, std::uint16_t ver,
                                             std::uint32_t bank, std::size_t i) noexcept;

[[nodiscard]] FilterSendPlan plan_filter_banks(const FilterBank& horiz, const FilterBank& vert,
                                               std::uint16_t ver, bool send_horiz,
                                               bool send_vert) noexcept;

}  // namespace mister::svc
