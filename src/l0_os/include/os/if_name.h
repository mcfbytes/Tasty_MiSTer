// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::os {

class IfName {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kMaxBytes = 15;
    static constexpr std::size_t kBufSize = kMaxBytes + 1;

    [[nodiscard]] static constexpr Ex<IfName> parse(std::string_view name) noexcept {
        if (name.empty() || name.size() > kMaxBytes)
            return std::unexpected(invalid());
        IfName out;
        for (std::size_t i = 0; i < name.size(); ++i) {
            if (!admitted(name[i]))
                return std::unexpected(invalid());
            out.text_[i] = name[i];
        }
        out.length_ = static_cast<std::uint8_t>(name.size());
        return out;
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return {text_.data(), length_};
    }

    [[nodiscard]] constexpr const char* c_str() const noexcept { return text_.data(); }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return length_; }

    friend constexpr bool operator==(const IfName&, const IfName&) = default;

private:
    [[nodiscard]] static constexpr bool admitted(char c) noexcept {
        return (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9')
            || c == '.'
            || c == '_'
            || c == '-';
    }

    [[nodiscard]] static constexpr Error invalid() noexcept {
        return Error{Errc::bad_format, ERR_SITE(), 0};
    }

    std::array<char, kBufSize> text_{};
    std::uint8_t length_ = 0;
};

static_assert(std::is_trivially_copyable_v<IfName>);
static_assert(std::has_unique_object_representations_v<IfName>);
static_assert(sizeof(IfName) == IfName::kBufSize + 1);

}
