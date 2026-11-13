// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "infra/decimal_text.h"
#include "infra/error.h"

namespace mister::os {

class WpaNetworkId {
public:
    static constexpr std::uint32_t kRadix = 10;
    static constexpr std::size_t kMaxDigits = 9;
    static constexpr char kZero = '0';
    static constexpr char kNine = '9';

    [[nodiscard]] static constexpr Ex<WpaNetworkId> of(int id) noexcept {
        if (id < 0)
            return std::unexpected(invalid());
        return WpaNetworkId{id};
    }

    [[nodiscard]] static constexpr Ex<WpaNetworkId> parse(std::string_view text) noexcept {
        if (text.empty() || text.size() > kMaxDigits)
            return std::unexpected(invalid());
        int id = 0;
        for (const char c : text) {
            if (c < kZero || c > kNine)
                return std::unexpected(invalid());
            id = id * static_cast<int>(kRadix) + (c - kZero);
        }
        return of(id);
    }

    [[nodiscard]] constexpr int value() const noexcept { return value_; }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{value_}; }

    friend constexpr bool operator==(const WpaNetworkId&, const WpaNetworkId&) = default;

private:
    [[nodiscard]] static constexpr Error invalid() noexcept {
        return Error{Errc::bad_format, ERR_SITE(), 0};
    }

    constexpr explicit WpaNetworkId(int value) noexcept : value_(value) {}

    int value_ = 0;
};

static_assert(std::is_trivially_copyable_v<WpaNetworkId>);
static_assert(std::has_unique_object_representations_v<WpaNetworkId>);
static_assert(sizeof(WpaNetworkId) == sizeof(int));

}
