// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <charconv>
#include <concepts>

namespace mister::infra {

class HexText {
public:
    template <std::unsigned_integral I>
    explicit HexText(I n) noexcept {
        (void)std::to_chars(buf_.data(), buf_.data() + buf_.size() - 1, n, kRadix);
    }

    [[nodiscard]] const char* c_str() const noexcept { return buf_.data(); }

private:
    static constexpr int kRadix = 16;
    std::array<char, 17> buf_{};
};

}
