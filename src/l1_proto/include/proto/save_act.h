// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "hal/types.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::proto {

inline constexpr std::size_t kSavePieceBytes = 512;

struct SaveAct {
    TASTY_SEAT_EXEMPT(component);

    enum class Kind : std::uint8_t { Done, Open, Read, Close };
    Kind kind = Kind::Done;
    bool ok = false;
    WideIoIndex index{};
    std::uint16_t len = 0;
};

static_assert(std::is_trivially_copyable_v<SaveAct>);

[[nodiscard]] constexpr std::uint32_t save_act_words(const SaveAct& a, hal::Width w) noexcept {
    switch (a.kind) {
        case SaveAct::Kind::Done:
            return 0;
        case SaveAct::Kind::Open:
            return 4;
        case SaveAct::Kind::Read:
            return 1u + (w == hal::Width::Word ? (a.len + 1u) / 2u : a.len);
        case SaveAct::Kind::Close:
            return 2;
    }
    return 0;
}

}
