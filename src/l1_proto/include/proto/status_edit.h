// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "proto/bit_field.h"
#include "proto/status_bit.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::proto {

struct StatusEdit {

    StatusWord bits{};
    StatusWord mask{};

    [[nodiscard]] static constexpr StatusEdit seeded(const StatusWord& base) noexcept {
        return {.bits = base, .mask = {}};
    }

    [[nodiscard]] constexpr StatusEdit then(const StatusEdit& next) const noexcept {
        return {
            .bits = next.over(bits),
            .mask = StatusEdit{.bits = next.mask, .mask = next.mask}.over(mask)
        };
    }

    [[nodiscard]] static constexpr StatusEdit bit(StatusBit b, bool value) noexcept {
        return field(BitField{b, 1}, FieldValue::flag(value));
    }

    [[nodiscard]] static constexpr StatusEdit field(BitField bits, FieldValue value) noexcept {
        StatusEdit e{};
        if (!bits.valid())
            return e;
        if (!bits.fits_status()) {
            fatal(Error{Errc::slot_range, ERR_SITE(), bits.start.index()}, "status edit");
        }
        for (unsigned i = 0; i < bits.width; ++i) {
            const auto n = bits.start.plus(i);
            if (!n)
                break;
            e.mask = e.mask.with(*n, true);
            if (i < BitField::kWordBits && ((value.v >> i) & 1u) != 0u)
                e.bits = e.bits.with(*n, true);
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
