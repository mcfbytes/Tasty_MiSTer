// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "infra/seat.h"
#include "svc/disc_ask.h"
#include "svc/disc_reader.h"
#include "svc/sector_header.h"
#include "svc/track.h"
#include "svc/types.h"

namespace mister::svc {

inline constexpr std::size_t kDiscSlotBytes = kFullFrameBytes;

constexpr std::size_t disc_form_bytes(DiscForm form) noexcept {
    switch (form) {
        case DiscForm::UserData:
            return kUserDataBytes;
        case DiscForm::RawFrame:
            return kRawFrameBytes;
        case DiscForm::Subcode:
            return kSubcodeBytes;
        case DiscForm::FullFrame:
            return kFullFrameBytes;
    }
    return 0;
}

inline constexpr std::size_t kDiscReadDepth = 16;

struct DiscReadSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    Generation gen{};
    proto::Lba lba{};
    std::uint16_t want = 0;

    std::uint16_t got = 0;
    DiscForm form = DiscForm::RawFrame;

    std::optional<SectorHeader> header{};
    bool ok = false;
    std::byte data[kDiscSlotBytes]{};

    [[nodiscard]] constexpr DiscAsk ask() const noexcept { return DiscAsk{form, lba, header}; }
};

static_assert(std::is_trivially_copyable_v<DiscReadSlot>);

}
