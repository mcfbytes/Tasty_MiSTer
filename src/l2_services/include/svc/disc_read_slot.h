// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "svc/disc_reader.h"

namespace mister::svc {

inline constexpr std::size_t kDiscSlotBytes = 2448;

constexpr std::size_t disc_form_bytes(DiscForm form) noexcept {
    switch (form) {
        case DiscForm::UserData:
            return 2048;
        case DiscForm::RawFrame:
            return 2352;
        case DiscForm::Subcode:
            return 96;
        case DiscForm::FullFrame:
            return kDiscSlotBytes;
    }
    return 0;
}

inline constexpr std::size_t kDiscReadDepth = 16;

struct DiscReadSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    std::uint32_t gen = 0;
    std::uint32_t lba = 0;
    std::uint16_t want = 0;
    std::uint16_t got = 0;
    DiscForm form = DiscForm::RawFrame;
    std::uint8_t ok = 0;
    std::byte data[kDiscSlotBytes]{};
};

static_assert(std::is_trivially_copyable_v<DiscReadSlot>);

}  // namespace mister::svc
