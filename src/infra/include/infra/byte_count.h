// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "infra/affine.h"
#include "infra/error.h"

namespace mister::infra {

struct ByteCount {
    std::size_t v = 0;
    using affine_count = ByteCount;

    [[nodiscard]] static constexpr ByteCount of(std::uint64_t bytes) noexcept {
        return ByteCount{static_cast<std::size_t>(bytes)};
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    template <class Buf>
        requires(sizeof(typename Buf::value_type) == sizeof(char))
    void resize_in(Buf& buf) const {
        buf.resize(static_cast<std::size_t>(v));
    }

    template <class Byte>
        requires(sizeof(Byte) == 1)
    [[nodiscard]] constexpr bool fits_in(std::span<Byte> buf) const noexcept {
        return v <= buf.size();
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    template <class Byte>
        requires(sizeof(Byte) == 1)
    void zero_past_in(std::span<Byte> buf) const noexcept {
        if (v < buf.size())
            std::memset(buf.data() + v, 0, buf.size() - v);
    }

    template <class Byte>
        requires(sizeof(Byte) == 1)
    [[nodiscard]] constexpr std::span<Byte> past_in(std::span<Byte> buf) const noexcept {
        return buf.subspan(v < buf.size() ? v : buf.size());
    }

    template <class Byte>
        requires(sizeof(Byte) == 1)
    [[nodiscard]] constexpr std::span<Byte> first_in(std::span<Byte> buf) const noexcept {
        return buf.first(v < buf.size() ? v : buf.size());
    }

    template <class Byte>
        requires(sizeof(Byte) == 1)
    [[nodiscard]] constexpr bool covers(std::span<Byte> buf) const noexcept {
        return v >= buf.size();
    }

    template <class D>
    [[nodiscard]] constexpr D passing_time(D per_byte) const noexcept {
        return D{static_cast<typename D::rep>(v) * per_byte.count()};
    }

    [[nodiscard]] constexpr Error refusal(Errc code, ErrorSite at) const noexcept {
        return Error{code, at, static_cast<std::uint32_t>(v)};
    }

    friend constexpr bool operator==(ByteCount, ByteCount) = default;
    friend constexpr auto operator<=>(ByteCount, ByteCount) = default;
};

}
