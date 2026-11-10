// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "proto/bit_field.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::proto {

struct StatusEdit {
    StatusWord bits{};
    StatusWord mask{};

    [[nodiscard]] static constexpr StatusEdit bit(StatusBit b, bool value) noexcept {
        return field(BitField{b, 1}, FieldValue{value ? 1u : 0u});
    }

    [[nodiscard]] static constexpr StatusEdit field(BitField bits, FieldValue value) noexcept {
        return field(bits.start, bits.width, value);
    }

    [[nodiscard]] static constexpr StatusEdit field(
        StatusBit start,
        unsigned width,
        FieldValue value
    ) noexcept {
        StatusEdit e{};
        if (width == 0)
            return e;
        if (start.v >= StatusWord::kBits || width > StatusWord::kBits - start.v) {
            fatal(Error{Errc::slot_range, ERR_SITE(), start.v}, "status edit");
        }
        unsigned i = 0;
        for (const StatusBit n : BitField{start, static_cast<std::uint8_t>(width)}.each_bit()) {
            e.mask = e.mask.with(n, true);
            if (i < 32u && ((value.v >> i) & 1u) != 0u)
                e.bits = e.bits.with(n, true);
            ++i;
        }
        return e;
    }

    [[nodiscard]] static constexpr StatusEdit whole(const StatusWord& w) noexcept {
        return StatusEdit{.bits = w, .mask = StatusWord::all()};
    }

    [[nodiscard]] constexpr StatusWord over(const StatusWord& w) const noexcept {
        StatusWord out{};
        for (unsigned i = 0; i < StatusWord::kWords; ++i) {
            out.words[i] = static_cast<std::uint16_t>(
                (w.words[i] & ~mask.words[i]) | (bits.words[i] & mask.words[i])
            );
        }
        return out;
    }
};

}
