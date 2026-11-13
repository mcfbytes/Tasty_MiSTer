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

class PinCode {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kMaxBytes = 16;
    static constexpr char kPrintableFirst = ' ';
    static constexpr char kPrintableLast = '~';

    [[nodiscard]] static constexpr Ex<PinCode> parse(std::string_view text) noexcept {
        if (text.empty() || text.size() > kMaxBytes)
            return std::unexpected(invalid());
        PinCode out;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] < kPrintableFirst || text[i] > kPrintableLast) {
                return std::unexpected(invalid());
            }
            out.text_[i] = text[i];
        }
        out.length_ = static_cast<std::uint8_t>(text.size());
        return out;
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return {text_.data(), length_};
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return length_; }

    friend constexpr bool operator==(const PinCode&, const PinCode&) = default;

private:
    [[nodiscard]] static constexpr Error invalid() noexcept {
        return Error{Errc::bad_format, ERR_SITE(), 0};
    }

    std::array<char, kMaxBytes> text_{};
    std::uint8_t length_ = 0;
};

static_assert(std::is_trivially_copyable_v<PinCode>);
static_assert(std::has_unique_object_representations_v<PinCode>);
static_assert(sizeof(PinCode) == PinCode::kMaxBytes + 1);

}
