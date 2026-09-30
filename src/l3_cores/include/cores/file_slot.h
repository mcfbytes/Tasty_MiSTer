// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

class IRomsetAliasSource;

using proto::IoIndex;

enum class SlotRole : std::uint8_t { Image, Disc, Save };

struct FileSlot {
    TASTY_SEAT_EXEMPT(const_shared);
    IoIndex index;
    std::string_view extensions;
    std::string_view label;
    bool writable = false;
    bool required = false;
    SlotRole role = SlotRole::Image;

    bool romset_browse = false;
    bool no_enter = false;

    std::unique_ptr<IRomsetAliasSource> (*romset_aliases)() = nullptr;

    bool opens_zip = false;
};

[[nodiscard]] constexpr bool walks_romset(std::span<const FileSlot> slots, IoIndex index) {
    for (const FileSlot& s : slots) {
        if (s.index.v == (index.v & 0x3Fu) && s.romset_browse) return true;
    }
    return false;
}

[[nodiscard]] constexpr SlotRole role_of(std::span<const FileSlot> slots, IoIndex index) {
    for (const FileSlot& s : slots)
        if (s.index == index && !s.romset_browse) return s.role;
    return SlotRole::Image;
}

[[nodiscard]] constexpr std::size_t disc_rows(std::span<const FileSlot> slots) {
    std::size_t n = 0;
    for (const FileSlot& s : slots)
        n += s.role == SlotRole::Disc ? 1u : 0u;
    return n;
}

void a_staging_profile_declares_exactly_one_disc_slot();

consteval IoIndex disc_slot_of(std::span<const FileSlot> slots) {
    if (disc_rows(slots) != 1) a_staging_profile_declares_exactly_one_disc_slot();
    for (const FileSlot& s : slots)
        if (s.role == SlotRole::Disc) return s.index;
    return IoIndex{};
}

}  // namespace mister::cores
