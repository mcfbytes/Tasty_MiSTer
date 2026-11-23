// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/endian.h"

namespace mister::svc {

struct NicSlotWord {
    static constexpr std::size_t kBytes = sizeof(std::uint64_t);
    std::uint64_t v = 0;

    [[nodiscard]] static constexpr NicSlotWord load(std::span<const std::byte, kBytes> b) noexcept {
        return NicSlotWord{infra::load_le<std::uint64_t>(b)};
    }

    constexpr void store(std::span<std::byte, kBytes> out) const noexcept {
        infra::store_le<std::uint64_t>(out, v);
    }

    friend constexpr bool operator==(NicSlotWord, NicSlotWord) = default;
};

}
