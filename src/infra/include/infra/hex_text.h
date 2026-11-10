// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <charconv>
#include <concepts>
#include <cstdint>

namespace mister::infra {

enum class HexCase : std::uint8_t { Lower, Upper };

class HexText {
public:
    template <std::unsigned_integral I>
    explicit HexText(I n, HexCase letter_case = HexCase::Lower) noexcept {
        (void)std::to_chars(buf_.data(), buf_.data() + buf_.size() - 1, n, kRadix);
        if (letter_case == HexCase::Upper)
            upper_case();
    }

    [[nodiscard]] const char* c_str() const noexcept { return buf_.data(); }

private:
    void upper_case() noexcept {
        for (char& c : buf_) {
            switch (c) {
                case 'a':
                    c = 'A';
                    break;
                case 'b':
                    c = 'B';
                    break;
                case 'c':
                    c = 'C';
                    break;
                case 'd':
                    c = 'D';
                    break;
                case 'e':
                    c = 'E';
                    break;
                case 'f':
                    c = 'F';
                    break;
                default:
                    break;
            }
        }
    }

    static constexpr int kRadix = 16;
    std::array<char, 17> buf_{};
};

}
