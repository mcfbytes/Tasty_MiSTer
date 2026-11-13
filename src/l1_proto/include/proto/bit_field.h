// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

#include "proto/status_bit.h"
#include "proto/types.h"

namespace mister::proto {

struct StatusWord;

enum class ExShift : std::uint8_t { None = 0, Plus32 = 1 };

struct BitField {
    StatusBit start{};
    std::uint8_t width = 0;

    static constexpr unsigned kWordBits = 32;

    constexpr bool valid() const noexcept { return width != 0; }

    [[nodiscard]] constexpr bool fits_status() const noexcept {
        return valid() && start.plus(width - 1u).has_value();
    }

    [[nodiscard]] constexpr std::optional<StatusBit> end() const noexcept {
        if (!valid())
            return std::nullopt;
        return start.plus(width - 1u);
    }

    constexpr std::uint32_t mask() const noexcept {
        return width >= kWordBits ? std::numeric_limits<std::uint32_t>::max()
                                  : ((1u << width) - 1u);
    }

    constexpr FieldCount slots() const noexcept {
        return FieldCount::of(std::uint64_t{1} << (width < kWordBits ? width : kWordBits));
    }

    [[nodiscard]] constexpr FieldValue read(const StatusWord& word) const noexcept;

    [[nodiscard]] constexpr StatusWord write(StatusWord word, FieldValue value) const noexcept;
    friend constexpr bool operator==(BitField, BitField) = default;
};

static_assert(sizeof(BitField) == 2);

inline constexpr BitField kResetField{kResetBit, 1};

[[nodiscard]] BitField decode_bit_ref(std::string_view spec, ExShift ex) noexcept;

[[nodiscard]] StatusBit decode_hd_selector(std::string_view after_letter) noexcept;

}
