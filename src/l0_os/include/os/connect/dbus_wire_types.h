// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

#include "infra/affine.h"
#include "infra/decimal_text.h"
#include "infra/index_range.h"
#include "os/connect/dbus_serial.h"

namespace mister::os::connect {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator/;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct WireBytes {
    std::size_t v = 0;
    using affine_count = WireBytes;

    static const WireBytes kNone;

    [[nodiscard]] constexpr auto indices() const noexcept;

    [[nodiscard]] constexpr WireBytes operator+(WireBytes more) const noexcept {
        return WireBytes{v + more.v};
    }

    [[nodiscard]] constexpr WireBytes operator-(WireBytes less) const noexcept {
        return WireBytes{v - less.v};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr WireBytes next() const noexcept { return WireBytes{v + 1}; }

    [[nodiscard]] constexpr WireBytes previous() const noexcept { return WireBytes{v - 1}; }

    friend constexpr bool operator==(WireBytes, WireBytes) = default;
    friend constexpr auto operator<=>(WireBytes, WireBytes) = default;
};

inline constexpr WireBytes WireBytes::kNone{0};

struct BufferOffset {
    std::size_t v = 0;
    using affine_count = WireBytes;
    using affine_index = BufferOffset;

    static const BufferOffset kStart;

    [[nodiscard]] constexpr BufferOffset operator+(WireBytes by) const noexcept {
        return BufferOffset{v + by.v};
    }

    [[nodiscard]] constexpr WireBytes since(BufferOffset from) const noexcept {
        return WireBytes{v - from.v};
    }

    [[nodiscard]] constexpr std::span<std::byte> at(std::span<std::byte> store) const noexcept {
        return store.subspan(v);
    }

    [[nodiscard]] constexpr std::span<const std::byte> at(
        std::span<const std::byte> store
    ) const noexcept {
        return store.subspan(v);
    }

    [[nodiscard]] constexpr std::span<char> at(std::span<char> store) const noexcept {
        return store.subspan(v);
    }

    [[nodiscard]] constexpr std::span<const char> at(std::span<const char> store) const noexcept {
        return store.subspan(v);
    }

    [[nodiscard]] constexpr std::span<const std::byte> up_to(
        BufferOffset end,
        std::span<const std::byte> store
    ) const noexcept {
        return store.subspan(v, end.v - v);
    }

    [[nodiscard]] constexpr std::span<const char> up_to(
        BufferOffset end,
        std::span<const char> store
    ) const noexcept {
        return store.subspan(v, end.v - v);
    }

    template <class Store>
    [[nodiscard]] constexpr decltype(auto) element_at(Store&& store) const noexcept {
        return store[v];
    }

    [[nodiscard]] constexpr BufferOffset mirrored_in(WireBytes width) const noexcept {
        return BufferOffset{width.v - 1 - v};
    }

    [[nodiscard]] static constexpr BufferOffset least(BufferOffset a, BufferOffset b) noexcept {
        return a.v <= b.v ? a : b;
    }

    friend constexpr bool operator==(BufferOffset, BufferOffset) = default;
    friend constexpr auto operator<=>(BufferOffset, BufferOffset) = default;
};

inline constexpr BufferOffset BufferOffset::kStart{0};

constexpr auto WireBytes::indices() const noexcept {
    return infra::affine::indices<BufferOffset>(*this);
}

struct WireU8 {
    std::uint8_t v = 0;

    using Bytes = std::array<std::byte, sizeof(std::uint8_t)>;

    static constexpr WireBytes kWidth{sizeof(std::uint8_t)};

    static constexpr WireBytes kMaxCount{std::numeric_limits<std::uint8_t>::max()};

    [[nodiscard]] constexpr Bytes le_bytes() const noexcept {
        return Bytes{static_cast<std::byte>(v)};
    }

    [[nodiscard]] static constexpr WireU8 of_le(std::span<const std::byte> in) noexcept {
        return WireU8{std::to_integer<std::uint8_t>(in[0])};
    }

    [[nodiscard]] constexpr char as_char() const noexcept { return static_cast<char>(v); }

    constexpr void store_le(std::span<std::byte> out) const noexcept { out[0] = le_bytes()[0]; }

    [[nodiscard]] constexpr std::string_view text_of(std::span<const char> store) const noexcept {
        return std::string_view{store.data(), v};
    }

    friend constexpr bool operator==(WireU8, WireU8) = default;
    friend constexpr auto operator<=>(WireU8, WireU8) = default;
};

struct WireU16 {
    std::uint16_t v = 0;

    using Bytes = std::array<std::byte, sizeof(std::uint16_t)>;
    static constexpr WireBytes kWidth{sizeof(std::uint16_t)};

    [[nodiscard]] constexpr Bytes le_bytes() const noexcept { return std::bit_cast<Bytes>(v); }

    [[nodiscard]] static constexpr WireU16 of_le(std::span<const std::byte> in) noexcept {
        Bytes raw{};
        for (const BufferOffset i : kWidth.indices())
            i.element_at(raw) = i.element_at(in);
        return WireU16{std::bit_cast<std::uint16_t>(raw)};
    }

    [[nodiscard]] static constexpr WireU16 of_be(std::span<const std::byte> in) noexcept {
        Bytes raw{};
        for (const BufferOffset i : kWidth.indices())
            i.mirrored_in(kWidth).element_at(raw) = i.element_at(in);
        return WireU16{std::bit_cast<std::uint16_t>(raw)};
    }

    constexpr void store_le(std::span<std::byte> out) const noexcept {
        const Bytes raw = le_bytes();
        for (const BufferOffset i : kWidth.indices())
            i.element_at(out) = i.element_at(raw);
    }

    [[nodiscard]] constexpr infra::DecimalText decimal() const noexcept {
        return infra::DecimalText{v};
    }

    [[nodiscard]] constexpr std::string_view text_of(std::span<const char> store) const noexcept {
        return std::string_view{store.data(), v};
    }

    friend constexpr bool operator==(WireU16, WireU16) = default;
    friend constexpr auto operator<=>(WireU16, WireU16) = default;
};

struct WireU32 {
    std::uint32_t v = 0;

    using Bytes = std::array<std::byte, sizeof(std::uint32_t)>;
    static constexpr WireBytes kWidth{sizeof(std::uint32_t)};

    static constexpr WireBytes kMaxCount{std::numeric_limits<std::uint32_t>::max()};

    [[nodiscard]] constexpr Bytes le_bytes() const noexcept { return std::bit_cast<Bytes>(v); }

    [[nodiscard]] static constexpr WireU32 of_le(std::span<const std::byte> in) noexcept {
        Bytes raw{};
        for (const BufferOffset i : kWidth.indices())
            i.element_at(raw) = i.element_at(in);
        return WireU32{std::bit_cast<std::uint32_t>(raw)};
    }

    [[nodiscard]] static constexpr WireU32 of_be(std::span<const std::byte> in) noexcept {
        Bytes raw{};
        for (const BufferOffset i : kWidth.indices())
            i.mirrored_in(kWidth).element_at(raw) = i.element_at(in);
        return WireU32{std::bit_cast<std::uint32_t>(raw)};
    }

    constexpr void store_le(std::span<std::byte> out) const noexcept {
        const Bytes raw = le_bytes();
        for (const BufferOffset i : kWidth.indices())
            i.element_at(out) = i.element_at(raw);
    }

    [[nodiscard]] constexpr infra::DecimalText decimal() const noexcept {
        return infra::DecimalText{v};
    }

    friend constexpr bool operator==(WireU32, WireU32) = default;
    friend constexpr auto operator<=>(WireU32, WireU32) = default;
};

struct WireU64 {
    std::uint64_t v = 0;

    using Bytes = std::array<std::byte, sizeof(std::uint64_t)>;
    static constexpr WireBytes kWidth{sizeof(std::uint64_t)};

    [[nodiscard]] constexpr Bytes le_bytes() const noexcept { return std::bit_cast<Bytes>(v); }

    [[nodiscard]] static constexpr WireU64 of_le(std::span<const std::byte> in) noexcept {
        Bytes raw{};
        for (const BufferOffset i : kWidth.indices())
            i.element_at(raw) = i.element_at(in);
        return WireU64{std::bit_cast<std::uint64_t>(raw)};
    }

    [[nodiscard]] static constexpr WireU64 of_le(
        std::span<const std::byte> in,
        WireBytes width
    ) noexcept {
        Bytes raw{};
        for (const BufferOffset i : width.indices())
            i.element_at(raw) = i.element_at(in);
        return WireU64{std::bit_cast<std::uint64_t>(raw)};
    }

    [[nodiscard]] static constexpr WireU64 of_be(
        std::span<const std::byte> in,
        WireBytes width
    ) noexcept {
        Bytes raw{};
        for (const BufferOffset i : width.indices())
            i.mirrored_in(width).element_at(raw) = i.element_at(in);
        return WireU64{std::bit_cast<std::uint64_t>(raw)};
    }

    [[nodiscard]] static constexpr WireU64 of_be(std::span<const std::byte> in) noexcept {
        Bytes raw{};
        for (const BufferOffset i : kWidth.indices())
            i.mirrored_in(kWidth).element_at(raw) = i.element_at(in);
        return WireU64{std::bit_cast<std::uint64_t>(raw)};
    }

    constexpr void store_le(std::span<std::byte> out) const noexcept {
        const Bytes raw = le_bytes();
        for (const BufferOffset i : kWidth.indices())
            i.element_at(out) = i.element_at(raw);
    }

    [[nodiscard]] constexpr WireU8 low_u8() const noexcept {
        return WireU8{static_cast<std::uint8_t>(v)};
    }

    [[nodiscard]] constexpr WireU16 low_u16() const noexcept {
        return WireU16{static_cast<std::uint16_t>(v)};
    }

    [[nodiscard]] constexpr WireU32 low_u32() const noexcept {
        return WireU32{static_cast<std::uint32_t>(v)};
    }

    friend constexpr bool operator==(WireU64, WireU64) = default;
    friend constexpr auto operator<=>(WireU64, WireU64) = default;
};

struct WireI16 {
    std::int16_t v = 0;

    using Bytes = std::array<std::byte, sizeof(std::int16_t)>;
    static constexpr WireBytes kWidth{sizeof(std::int16_t)};

    [[nodiscard]] constexpr Bytes le_bytes() const noexcept { return std::bit_cast<Bytes>(v); }

    constexpr void store_le(std::span<std::byte> out) const noexcept {
        const Bytes raw = le_bytes();
        for (const BufferOffset i : kWidth.indices())
            i.element_at(out) = i.element_at(raw);
    }

    friend constexpr bool operator==(WireI16, WireI16) = default;
    friend constexpr auto operator<=>(WireI16, WireI16) = default;
};

struct WireI32 {
    std::int32_t v = 0;

    using Bytes = std::array<std::byte, sizeof(std::int32_t)>;
    static constexpr WireBytes kWidth{sizeof(std::int32_t)};

    [[nodiscard]] constexpr Bytes le_bytes() const noexcept { return std::bit_cast<Bytes>(v); }

    constexpr void store_le(std::span<std::byte> out) const noexcept {
        const Bytes raw = le_bytes();
        for (const BufferOffset i : kWidth.indices())
            i.element_at(out) = i.element_at(raw);
    }

    friend constexpr bool operator==(WireI32, WireI32) = default;
    friend constexpr auto operator<=>(WireI32, WireI32) = default;
};

struct WireI64 {
    std::int64_t v = 0;

    using Bytes = std::array<std::byte, sizeof(std::int64_t)>;
    static constexpr WireBytes kWidth{sizeof(std::int64_t)};

    [[nodiscard]] constexpr Bytes le_bytes() const noexcept { return std::bit_cast<Bytes>(v); }

    constexpr void store_le(std::span<std::byte> out) const noexcept {
        const Bytes raw = le_bytes();
        for (const BufferOffset i : kWidth.indices())
            i.element_at(out) = i.element_at(raw);
    }

    friend constexpr bool operator==(WireI64, WireI64) = default;
    friend constexpr auto operator<=>(WireI64, WireI64) = default;
};

[[nodiscard]] constexpr WireI16 as_signed(WireU16 bits) noexcept {
    return WireI16{static_cast<std::int16_t>(bits.v)};
}

[[nodiscard]] constexpr WireI32 as_signed(WireU32 bits) noexcept {
    return WireI32{static_cast<std::int32_t>(bits.v)};
}

[[nodiscard]] constexpr WireI64 as_signed(WireU64 bits) noexcept {
    return WireI64{static_cast<std::int64_t>(bits.v)};
}

[[nodiscard]] constexpr WireBytes as_bytes(WireU32 length) noexcept {
    return WireBytes{length.v};
}

[[nodiscard]] constexpr WireBytes as_bytes(WireU8 length) noexcept {
    return WireBytes{length.v};
}

[[nodiscard]] constexpr WireU16 as_u16(WireBytes count) noexcept {
    return WireU16{static_cast<std::uint16_t>(count.v)};
}

[[nodiscard]] constexpr WireU32 as_u32(WireBytes count) noexcept {
    return WireU32{static_cast<std::uint32_t>(count.v)};
}

[[nodiscard]] constexpr WireU8 as_u8(WireBytes count) noexcept {
    return WireU8{static_cast<std::uint8_t>(count.v)};
}

[[nodiscard]] constexpr WireBytes left_in(std::span<std::byte> store, BufferOffset at) noexcept {
    return WireBytes{store.size() - at.v};
}

[[nodiscard]] constexpr WireBytes left_in(
    std::span<const std::byte> store,
    BufferOffset at
) noexcept {
    return WireBytes{store.size() - at.v};
}

[[nodiscard]] constexpr BufferOffset end_of(std::span<const std::byte> store) noexcept {
    return BufferOffset{store.size()};
}

[[nodiscard]] constexpr BufferOffset end_of(std::span<const char> store) noexcept {
    return BufferOffset{store.size()};
}

[[nodiscard]] constexpr BufferOffset as_offset(WireBytes count) noexcept {
    return BufferOffset{count.v};
}

[[nodiscard]] constexpr WireU8 as_wire(std::byte b) noexcept {
    return WireU8{std::to_integer<std::uint8_t>(b)};
}

[[nodiscard]] constexpr WireU8 as_byte(char c) noexcept {
    return WireU8{static_cast<std::uint8_t>(c)};
}

template <class T>
[[nodiscard]] constexpr T& nth(std::span<T> table, WireBytes index) noexcept {
    return table[index.v];
}

[[nodiscard]] constexpr char& text_at(std::span<char> store, BufferOffset at) noexcept {
    return store[at.v];
}

[[nodiscard]] constexpr char text_at(std::span<const char> store, BufferOffset at) noexcept {
    return store[at.v];
}

enum : std::uint8_t { kHexRadix = 16, kHexLetterStart = 10 };

[[nodiscard]] constexpr std::array<char, 2> hex_pair(WireU8 byte) noexcept {
    const std::uint8_t high = static_cast<std::uint8_t>(byte.v / kHexRadix);
    const std::uint8_t low = static_cast<std::uint8_t>(byte.v % kHexRadix);
    const char high_digit = high < kHexLetterStart
        ? static_cast<char>('0' + high)
        : static_cast<char>('a' + (high - kHexLetterStart));
    const char low_digit = low < kHexLetterStart ? static_cast<char>('0' + low)
                                                 : static_cast<char>('a' + (low - kHexLetterStart));
    return std::array<char, 2>{high_digit, low_digit};
}

[[nodiscard]] constexpr WireU32 as_wire(DbusSerial serial) noexcept {
    return WireU32{serial.value()};
}

[[nodiscard]] constexpr DbusSerial as_serial(WireU32 serial) noexcept {
    return DbusSerial::of(serial.v).value_or(DbusSerial{});
}

[[nodiscard]] constexpr WireBytes padding_to(BufferOffset at, WireBytes boundary) noexcept {
    return WireBytes{(boundary.v - (at.v % boundary.v)) % boundary.v};
}

[[nodiscard]] constexpr BufferOffset advance(BufferOffset at, WireBytes count) noexcept {
    return BufferOffset{at.v + count.v};
}

}
