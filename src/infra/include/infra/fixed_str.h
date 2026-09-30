// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "infra/seat.h"

namespace mister {

enum class StrFit : std::uint8_t {
    Clip,
    Reject,
};

template <std::size_t N, StrFit kFit>
class FixedStr {
    TASTY_SEAT_EXEMPT(component);
    static_assert(N >= 2, "a FixedStr holds at least one byte and its NUL");
    static_assert(N <= 65535, "len_ is 16 bits");

public:
    static constexpr std::size_t kBufSize = N;
    static constexpr std::size_t kCapacity = N - 1;
    static constexpr StrFit kPolicy = kFit;

    constexpr FixedStr() noexcept = default;

    [[nodiscard]] constexpr bool assign(std::string_view s) noexcept {
        const bool over = s.size() > kCapacity;
        if constexpr (kFit == StrFit::Reject) {
            if (over) return false;
        }
        const std::size_t n = over ? kCapacity : s.size();
        std::copy_n(s.data(), n, buf_);
        buf_[n] = '\0';
        len_ = static_cast<std::uint16_t>(n);
        clipped_ = over;
        return true;
    }

    [[nodiscard]] constexpr bool append(std::string_view s) noexcept {
        const std::size_t room = kCapacity - len_;
        const bool over = s.size() > room;
        if constexpr (kFit == StrFit::Reject) {
            if (over) return false;
        }
        const std::size_t n = over ? room : s.size();
        std::copy_n(s.data(), n, buf_ + len_);
        len_ = static_cast<std::uint16_t>(len_ + n);
        buf_[len_] = '\0';
        clipped_ = clipped_ || over;
        return true;
    }

    constexpr void clear() noexcept {
        buf_[0] = '\0';
        len_ = 0;
        clipped_ = false;
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return std::string_view{buf_, len_};
    }

    [[nodiscard]] constexpr const char* c_str() const noexcept { return buf_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return len_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return len_ == 0; }

    [[nodiscard]] constexpr bool clipped() const noexcept { return clipped_; }

    friend constexpr bool operator==(const FixedStr& a, const FixedStr& b) noexcept {
        return a.view() == b.view();
    }

private:
    char buf_[N]{};
    std::uint16_t len_ = 0;
    bool clipped_ = false;
    std::uint8_t reserved_ = 0;
};

static_assert(std::is_trivially_copyable_v<FixedStr<8, StrFit::Clip>>);
static_assert(std::is_standard_layout_v<FixedStr<8, StrFit::Clip>>);
static_assert(std::is_trivially_destructible_v<FixedStr<8, StrFit::Reject>>);
static_assert(std::has_unique_object_representations_v<FixedStr<1024, StrFit::Reject>>);

}  // namespace mister
