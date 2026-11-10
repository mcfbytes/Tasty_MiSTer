// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "cores/types.h"
#include "proto/types.h"

namespace mister::cores::st_ctrl {

struct StSlot {
    std::uint8_t v = 0;
    inline static constexpr std::uint8_t kCount = 6;

    static const StSlot FloppyA;
    static const StSlot FloppyB;
    static const StSlot Acsi0;
    static const StSlot Acsi1;
    static const StSlot Tos;
    static const StSlot Cart;

    [[nodiscard]] static constexpr std::optional<StSlot> of(proto::IoIndex io) noexcept {
        if (!io.below<kCount>())
            return std::nullopt;
        return StSlot{io.v};
    }

    [[nodiscard]] constexpr proto::IoIndex io() const noexcept { return proto::IoIndex{v}; }

    [[nodiscard]] constexpr bool is_floppy() const noexcept {
        return *this == FloppyA || *this == FloppyB;
    }

    [[nodiscard]] constexpr std::optional<AcsiTarget> acsi_target() const noexcept {
        if (*this == Acsi0)
            return AcsiTarget{0};
        if (*this == Acsi1)
            return AcsiTarget{1};
        return std::nullopt;
    }

    [[nodiscard]] constexpr bool is_disk() const noexcept {
        return is_floppy() || acsi_target().has_value();
    }

    friend constexpr bool operator==(StSlot, StSlot) = default;
};

inline constexpr StSlot StSlot::FloppyA{0};
inline constexpr StSlot StSlot::FloppyB{1};
inline constexpr StSlot StSlot::Acsi0{2};
inline constexpr StSlot StSlot::Acsi1{3};
inline constexpr StSlot StSlot::Tos{4};
inline constexpr StSlot StSlot::Cart{5};

inline constexpr std::array<StSlot, 2> kAcsiSlots{StSlot::Acsi0, StSlot::Acsi1};

}
