// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "proto/status_edit.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::cores::n64 {

struct StatusField {
    std::uint8_t lo = 0;
    std::uint8_t width = 1;

    [[nodiscard]] constexpr std::uint8_t hi() const noexcept {
        return static_cast<std::uint8_t>(lo + width - 1);
    }

    template <typename Bits>
    [[nodiscard]] constexpr proto::FieldValue read(const Bits& bits) const noexcept {
        std::uint32_t v = 0;
        for (std::uint8_t i = 0; i < width; ++i) {
            if (bits.get_bit(proto::StatusBit{static_cast<std::uint8_t>(lo + i)}))
                v |= 1u << i;
        }
        return proto::FieldValue{v};
    }

    [[nodiscard]] constexpr proto::FieldValue all() const noexcept {
        return proto::FieldValue{(1u << width) - 1u};
    }

    [[nodiscard]] constexpr proto::StatusWord written(
        proto::StatusWord w,
        proto::FieldValue v
    ) const noexcept {
        return proto::StatusEdit::field(proto::StatusBit{lo}, width, v).over(w);
    }
};

namespace opt {

inline constexpr StatusField kReset{0, 1};
inline constexpr StatusField kReloadSave{40, 1};
inline constexpr StatusField kSaveBackup{41, 1};
inline constexpr StatusField kAutosave{42, 1};
inline constexpr StatusField kAutoDetect{64, 1};
inline constexpr StatusField kCicType{65, 4};
inline constexpr StatusField kNoEpak{70, 1};
inline constexpr StatusField kCpak{71, 1};
inline constexpr StatusField kRpak{72, 1};
inline constexpr StatusField kTpak{73, 1};
inline constexpr StatusField kRtc{74, 1};
inline constexpr StatusField kSaveType{75, 3};
inline constexpr StatusField kSysType{79, 2};
inline constexpr StatusField kAutoPak{81, 1};
inline constexpr StatusField kPatches{90, 1};
inline constexpr StatusField kCheats{103, 1};
inline constexpr StatusField kDdDevelopment{107, 1};

inline constexpr std::array<StatusField, 4>
    kControllers{StatusField{49, 3}, StatusField{52, 3}, StatusField{55, 3}, StatusField{58, 3}};

}

}
