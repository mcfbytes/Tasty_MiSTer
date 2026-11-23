// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

#include "cores/minimig/lance_word.h"
#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/tally.h"
#include "svc/nic_slot_word.h"

namespace mister::cores::minimig {

struct A2065CommandCount {
    std::uint32_t v = 0;
    using affine_count = A2065CommandCount;

    [[nodiscard]] constexpr infra::Tally tally() const noexcept { return infra::Tally{v}; }

    friend constexpr bool operator==(A2065CommandCount, A2065CommandCount) = default;
    friend constexpr auto operator<=>(A2065CommandCount, A2065CommandCount) = default;
};

struct A2065CommandSeq {
    std::uint32_t v = 0;
    using affine_count = A2065CommandCount;

    [[nodiscard]] static constexpr A2065CommandSeq of(svc::NicSlotWord w) noexcept {
        return A2065CommandSeq{static_cast<std::uint32_t>(w.v)};
    }

    [[nodiscard]] constexpr infra::ByteCount slot_in(
        A2065CommandCount ring,
        infra::ByteCount stride
    ) const noexcept {
        return infra::ByteCount{(v % ring.v) * stride.v};
    }

    friend constexpr bool operator==(A2065CommandSeq, A2065CommandSeq) = default;
};

struct A2065Command {
    std::uint64_t v = 0;

    [[nodiscard]] static constexpr A2065Command of(svc::NicSlotWord entry) noexcept {
        return A2065Command{entry.v};
    }

    [[nodiscard]] constexpr LanceWord rap() const noexcept {
        constexpr unsigned kRapShift = 1;
        constexpr std::uint64_t kRapMask = 0x7F;
        return LanceWord{static_cast<std::uint16_t>((v >> kRapShift) & kRapMask)};
    }

    [[nodiscard]] constexpr LanceWord data() const noexcept {
        constexpr unsigned kDataShift = 8;
        constexpr std::uint64_t kDataMask = 0xFFFF;
        return LanceWord{static_cast<std::uint16_t>((v >> kDataShift) & kDataMask)};
    }

    friend constexpr bool operator==(A2065Command, A2065Command) = default;
};

}
