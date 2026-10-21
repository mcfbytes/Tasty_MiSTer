// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <type_traits>

#include "proto/types.h"
#include "ui/types.h"

namespace mister::ui {

struct SlotFocus final {
    enum class Kind : std::uint8_t { File, Image };

    Kind kind = Kind::File;
    proto::IoIndex index{};

    friend constexpr bool operator==(SlotFocus, SlotFocus) = default;
};

static_assert(
    std::is_same_v<decltype(proto::IoIndex::v), std::uint8_t>,
    "a wider ioctl index would collide with the focus kind bit"
);

inline constexpr std::uint16_t kSlotFocusKindBit = 0x100u;
inline constexpr std::uint16_t kSlotFocusPresentBit = 0x200u;

[[nodiscard]] constexpr ItemId to_item(SlotFocus f) noexcept {
    return ItemId{static_cast<std::uint16_t>(
        kSlotFocusPresentBit
        | (f.kind == SlotFocus::Kind::Image ? kSlotFocusKindBit : std::uint16_t{0})
        | f.index.v
    )};
}

[[nodiscard]] constexpr std::optional<SlotFocus> slot_focus_of(ItemId id) noexcept {
    if ((id.v & kSlotFocusPresentBit) == 0u || (id.v & 0xFC00u) != 0u)
        return std::nullopt;
    return SlotFocus{
        (id.v & kSlotFocusKindBit) != 0u ? SlotFocus::Kind::Image : SlotFocus::Kind::File,
        proto::IoIndex{static_cast<std::uint8_t>(id.v & 0xFFu)}
    };
}

static_assert(
    slot_focus_of(to_item({SlotFocus::Kind::File, proto::IoIndex{0}}))
    == SlotFocus{SlotFocus::Kind::File, proto::IoIndex{0}}
);
static_assert(
    slot_focus_of(to_item({SlotFocus::Kind::Image, proto::IoIndex{0xFF}}))
    == SlotFocus{SlotFocus::Kind::Image, proto::IoIndex{0xFF}}
);
static_assert(!(to_item({SlotFocus::Kind::File, proto::IoIndex{0}}) == kNoItem));
static_assert(!slot_focus_of(kNoItem).has_value());

}
