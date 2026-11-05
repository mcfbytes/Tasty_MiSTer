// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <limits>

namespace mister::infra {

class DecimalText {
public:
    template <std::integral I>
    explicit DecimalText(I n) noexcept {
        (void)std::to_chars(buf_.data(), buf_.data() + buf_.size() - 1, n);
    }

    [[nodiscard]] const char* c_str() const noexcept { return buf_.data(); }

private:
    std::array<char, std::numeric_limits<std::uint64_t>::digits10 + 2> buf_{};
};

}
