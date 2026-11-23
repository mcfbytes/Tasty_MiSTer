// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "cores/minimig/lance_csr.h"
#include "cores/minimig/lance_word.h"
#include "infra/byte_view.h"
#include "infra/crc32.h"
#include "svc/nic_mac.h"

namespace mister::cores::minimig {

struct LanceFilter {
    static constexpr std::size_t kQuarters = 4;

    using Quarters = std::array<LanceWord, kQuarters>;
    std::uint64_t v = 0;

    [[nodiscard]] static constexpr LanceFilter of(const Quarters& q) noexcept {
        std::uint64_t f = 0;
        for (std::size_t i = 0; i < kQuarters; ++i)
            f |= std::uint64_t{q[i].v} << (i * kQuarterBits);
        return LanceFilter{f};
    }

    [[nodiscard]] constexpr LanceFilter with_register(LanceCsr reg, LanceWord w) const noexcept {
        const std::size_t shift = static_cast<std::size_t>(LanceCsr::kFilterLast.v - reg.v)
            * kQuarterBits;
        const std::uint64_t mask = std::uint64_t{std::numeric_limits<std::uint16_t>::max()}
            << shift;
        return LanceFilter{(v & ~mask) | (std::uint64_t{w.v} << shift)};
    }

    [[nodiscard]] bool accepts(const svc::MacAddress& dst) const noexcept {
        constexpr unsigned kHashShift = 26;
        const std::uint32_t crc = infra::crc32_of(
            infra::byte_cast<const std::uint8_t>(std::span<const std::byte>(dst.bytes))
        );
        return (v & (std::uint64_t{1} << (~crc >> kHashShift))) != 0u;
    }

    friend constexpr bool operator==(LanceFilter, LanceFilter) = default;

private:
    static constexpr unsigned kQuarterBits = std::numeric_limits<std::uint16_t>::digits;
};

}
