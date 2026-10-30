// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::proto {

struct StatusEdit {
    StatusWord bits{};
    StatusWord mask{};

    [[nodiscard]] static constexpr StatusEdit bit(StatusBit b, bool value) noexcept {
        return field(b, 1, value ? 1u : 0u);
    }

    [[nodiscard]] static constexpr StatusEdit field(
        StatusBit start,
        unsigned width,
        std::uint32_t value
    ) noexcept {
        StatusEdit e{};
        if (width == 0)
            return e;
        if (start.v >= StatusWord::kBits || width > StatusWord::kBits - start.v) {
            fatal(Error{Errc::slot_range, ERR_SITE(), start.v}, "status edit");
        }
        for (unsigned i = 0; i < width; ++i) {
            const unsigned n = start.v + i;
            const auto m = static_cast<std::uint16_t>(1u << (n % 16u));
            e.mask.words[n / 16u] = static_cast<std::uint16_t>(e.mask.words[n / 16u] | m);
            if (i < 32u && ((value >> i) & 1u) != 0u) {
                e.bits.words[n / 16u] = static_cast<std::uint16_t>(e.bits.words[n / 16u] | m);
            }
        }
        return e;
    }

    [[nodiscard]] static constexpr StatusEdit whole(const StatusWord& w) noexcept {
        StatusEdit e{.bits = w, .mask = {}};
        for (auto& m : e.mask.words)
            m = 0xFFFFu;
        return e;
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
