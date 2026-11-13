// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "infra/fixed_str.h"
#include "svc/filter_bank.h"
#include "svc/filter_format.h"
#include "svc/filter_kernel.h"
#include "svc/filter_send_plan.h"
#include "svc/scaler_slot_mode.h"
#include "svc/types.h"
#include "svc/video_timing.h"

namespace mister::svc {

class Vfs;
struct FilterSet;

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

struct ScalerSlot {
    ScalerSlotMode mode{};

    FixedStr<kScalerNameCap, StrFit::Clip> name{};
};

using ScalerSeeds = ScalerSlots<std::string_view>;

[[nodiscard]] ScalerSlots<ScalerSlot> decode_scaler_cfg(
    std::span<const std::byte> blob,
    const ScalerSeeds& seeds
);

[[nodiscard]] std::unique_ptr<FilterSet> load_filter_set(
    const Vfs& vfs,
    std::string_view core,
    const ScalerSeeds& seeds
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
