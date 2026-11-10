// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/fixed_str.h"
#include "svc/filter_bank.h"
#include "svc/filter_format.h"
#include "svc/filter_kernel.h"
#include "svc/filter_send_plan.h"
#include "svc/types.h"
#include "svc/video_timing.h"

namespace mister::svc {

class Vfs;
struct FilterSet;

}

namespace mister::proto {
class Link;
}

namespace mister::svc {

[[nodiscard]] bool same_digest(const FilterBank& a, const FilterBank& b) noexcept;

enum class FilterParseOutcome : std::uint8_t {
    Parsed,
    NearestNeighbour,
    OverCap,
};

struct FilterParse {
    FilterBank bank;
    FilterParseOutcome outcome = FilterParseOutcome::Parsed;

    [[nodiscard]] constexpr bool parsed() const noexcept {
        return outcome == FilterParseOutcome::Parsed;
    }
};

[[nodiscard]] FilterParse parse_video_filter(std::string_view text);

[[nodiscard]] FilterBank nearest_neighbour_bank();

inline constexpr std::size_t kScalerNameCap = 1023;
inline constexpr std::size_t kScalerCfgBytes = kScalerSlots * (1 + kScalerNameCap);

struct ScalerSlotMode {
    std::uint8_t v = 0;
    static constexpr std::uint8_t kOff = 0;
    static constexpr std::uint8_t kFromFile = 1;

    [[nodiscard]] static constexpr ScalerSlotMode of(std::byte raw) noexcept {
        return ScalerSlotMode{std::to_integer<std::uint8_t>(raw)};
    }

    [[nodiscard]] static constexpr ScalerSlotMode off() noexcept { return ScalerSlotMode{kOff}; }

    [[nodiscard]] static constexpr ScalerSlotMode from_file() noexcept {
        return ScalerSlotMode{kFromFile};
    }

    [[nodiscard]] constexpr bool enabled() const noexcept { return v != kOff; }

    [[nodiscard]] constexpr bool legal() const noexcept { return v <= kFromFile; }

    [[nodiscard]] Ex<void> write_to(proto::Link& link) const;
    friend constexpr bool operator==(ScalerSlotMode, ScalerSlotMode) = default;
};

struct ScalerSlot {
    ScalerSlotMode mode{};

    FixedStr<kScalerNameCap, StrFit::Clip> name{};
};

using ScalerSeeds = ScalerSlots<std::string_view>;

[[nodiscard]] ScalerSlots<ScalerSlot> decode_scaler_cfg(
    std::span<const std::byte> blob,
    const ScalerSeeds& seeds
);

void load_filter_set(
    const Vfs& vfs,
    std::string_view core,
    const ScalerSeeds& seeds,
    FilterSet& out
);

[[nodiscard]] constexpr std::size_t filter_bank_word_count(FilterFormat fmt) noexcept {
    return detail::filter_words_per_bank(fmt);
}

[[nodiscard]] std::uint16_t filter_bank_word(
    std::span<const FilterPhase> phases,
    FilterFormat fmt,
    FilterWireBank bank,
    FilterBankWordIndex i
) noexcept;

[[nodiscard]] FilterSendPlan plan_filter_banks(
    const FilterBank& horiz,
    const FilterBank& vert,
    FilterFormat fmt,
    bool send_horiz,
    bool send_vert
) noexcept;

}
