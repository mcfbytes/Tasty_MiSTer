// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/span_cut.h"
#include "infra/indexed_span.h"
#include "infra/byte_offset.h"
#include "infra/byte_count.h"

namespace mister::cores::ide {

enum class AtapiSenseKey : std::uint8_t {
    NoSense = 0x0,
    NotReady = 0x2,
    IllegalRequest = 0x5,
    UnitAttention = 0x6,
};

enum class AtapiAsc : std::uint8_t {
    None = 0x00,
    BecomingReady = 0x04,
    LoadSettled = 0x1C,
    IllegalOpcode = 0x20,
    IllegalField = 0x24,
    MediumMayHaveChanged = 0x28,
    CommandSequence = 0x2C,
    MediumNotPresent = 0x3A,
};

enum class AtapiAscq : std::uint8_t { None = 0x00, InProgress = 0x01 };

[[nodiscard]] constexpr std::byte wire_byte(AtapiSenseKey key) noexcept {
    return static_cast<std::byte>(key);
}

[[nodiscard]] constexpr std::byte wire_byte(AtapiAsc asc) noexcept {
    return static_cast<std::byte>(asc);
}

[[nodiscard]] constexpr std::byte wire_byte(AtapiAscq ascq) noexcept {
    return static_cast<std::byte>(ascq);
}

[[nodiscard]] constexpr std::byte error_register_of(AtapiSenseKey key) noexcept {
    constexpr unsigned kKeyShift = 4;
    return wire_byte(key) << kKeyShift;
}

struct AtapiSense {

    static constexpr infra::ByteCount kBytes{18};
    static constexpr std::byte kCurrentErrors{0x70};
    static constexpr infra::ByteOffset kResponseCodeAt{0};
    static constexpr infra::ByteOffset kKeyAt{2};
    static constexpr infra::ByteOffset kAscAt{12};
    static constexpr infra::ByteOffset kAscqAt{13};

    AtapiSenseKey key = AtapiSenseKey::NoSense;
    AtapiAsc asc = AtapiAsc::None;
    AtapiAscq ascq = AtapiAscq::None;

    constexpr infra::ByteCount store(std::span<std::byte> out) const noexcept {
        const std::span<std::byte> block = infra::affine::first(out, kBytes);
        std::ranges::fill(block, std::byte{0});
        const infra::IndexedSpan<std::byte, infra::ByteOffset> field{block};
        field[kResponseCodeAt] = kCurrentErrors;
        field[kKeyAt] = wire_byte(key);
        field[kAscAt] = wire_byte(asc);
        field[kAscqAt] = wire_byte(ascq);
        return kBytes;
    }

    friend constexpr bool operator==(AtapiSense, AtapiSense) = default;
};

static_assert(sizeof(AtapiSense) == 3, "three wire bytes and nothing else");

}
