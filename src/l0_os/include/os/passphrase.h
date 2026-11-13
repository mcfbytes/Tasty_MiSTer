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

class Passphrase {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kMinBytes = 8;
    static constexpr std::size_t kMaxBytes = 63;
    static constexpr std::size_t kBufSize = kMaxBytes + 1;
    static constexpr std::size_t kQuotedMax = kMaxBytes + 2;
    static constexpr char kQuote = '"';

    [[nodiscard]] static Ex<Passphrase> parse(std::string_view text) noexcept;

    class Quoted {
    public:
        [[nodiscard]] std::string_view view() const noexcept { return {text_.data(), length_}; }

        [[nodiscard]] const char* c_str() const noexcept { return text_.data(); }

    private:
        friend class Passphrase;
        std::array<char, kQuotedMax + 1> text_{};
        std::size_t length_ = 0;
    };

    [[nodiscard]] Quoted write_quoted() const noexcept;

    void scrub() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return length_; }

    friend constexpr bool operator==(const Passphrase& a, const Passphrase& b) noexcept {
        return a.length_ == b.length_ && a.text_ == b.text_;
    }

private:
    std::array<char, kBufSize> text_{};
    std::uint8_t length_ = 0;
};

static_assert(std::is_trivially_copyable_v<Passphrase>);
static_assert(std::has_unique_object_representations_v<Passphrase>);
static_assert(sizeof(Passphrase) == Passphrase::kBufSize + 1);

}
