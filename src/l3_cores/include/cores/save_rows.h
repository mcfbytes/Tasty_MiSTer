// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "cores/n64_memory_type.h"
#include "cores/save_row_count.h"

namespace mister::cores::n64 {

struct SaveRows {
    MemoryType cart = MemoryType::None;
    std::array<SaveRowKind, kMaxSaveRows> kinds{};
    SaveRowCount count{};
};

[[nodiscard]] constexpr SaveRows plan_saves(MemoryType cart, bool tpak, bool cpak) noexcept {
    SaveRows out{};
    std::uint8_t n = 0;
    if (cart != MemoryType::None)
        out.kinds[n++] = SaveRowKind::Cart;
    if (tpak) {
        out.kinds[n++] = SaveRowKind::Tpak;
    } else if (cpak) {
        out.kinds[n++] = SaveRowKind::Pak1;
    }
    if (cpak) {
        out.kinds[n++] = SaveRowKind::Pak2;
        out.kinds[n++] = SaveRowKind::Pak3;
        out.kinds[n++] = SaveRowKind::Pak4;
    }
    out.cart = cart;
    out.count = SaveRowCount::of(n);
    return out;
}

}
