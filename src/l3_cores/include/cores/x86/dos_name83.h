// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace mister::cores::x86 {

struct Name83 {
    static constexpr std::size_t kBytes = 11;
    static constexpr std::size_t kNameBytes = 8;
    static constexpr std::size_t kExtBytes = 3;
    static constexpr char kPad = ' ';

    std::array<char, kBytes> text{};

    [[nodiscard]] static Name83 of(std::string_view host_name) noexcept;

    [[nodiscard]] bool matches(std::string_view host_name) const noexcept;

    [[nodiscard]] static constexpr Name83 volume_label() noexcept {
        Name83 out{};
        out.text = {'M', 'i', 'S', 'T', 'e', 'r', kPad, kPad, kPad, kPad, kPad};
        return out;
    }

    void store(std::span<std::byte, kBytes> out) const noexcept;

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return {text.data(), text.size()};
    }

    friend constexpr bool operator==(Name83, Name83) = default;
};

static_assert(Name83::kNameBytes + Name83::kExtBytes == Name83::kBytes);

}
