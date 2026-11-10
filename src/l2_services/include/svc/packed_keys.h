// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/indexed_array.h"
#include "svc/joy_bit.h"
#include "svc/sys_button.h"
#include "svc/types.h"

namespace mister::svc {

struct PackedKeys {
    EvdevCode lo{};
    EvdevCode hi{};

    [[nodiscard]] static constexpr PackedKeys lone(EvdevCode code) noexcept {
        return PackedKeys{code, EvdevCode{}};
    }

    friend constexpr bool operator==(PackedKeys, PackedKeys) = default;
};

static_assert(sizeof(PackedKeys) == 4, "a packed pair is one 32-bit joy-map word");
static_assert(sizeof(PackedKeys) == 2 * sizeof(EvdevCode), "two 16-bit halves, never widened");

using SysWords = infra::IndexedArray<PackedKeys, kJoyWords, SysButton>;
static_assert(sizeof(SysWords) == 128, "req: the system map is 32 packed words");

using WireWords = infra::IndexedArray<PackedKeys, kJoyWords, JoyBit>;
static_assert(sizeof(WireWords) == 128, "req: the Joystick blob is exactly 128 bytes");
template <class Array, class Index>
concept indexable_by = requires(Array a, Index i) { a[i]; };

static_assert(!indexable_by<SysWords, JoyBit>, "a wire word cannot subscript the system map");
static_assert(!indexable_by<WireWords, SysButton>, "a system word cannot subscript the wire map");

}
