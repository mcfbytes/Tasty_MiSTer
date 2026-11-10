// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>

#include "infra/byte_view.h"

namespace mister::app {

template <infra::ByteLike B, std::size_t N>
class TextField {
    static_assert(N >= 1, "a text field holds at least its NUL");

public:
    explicit TextField(std::span<B, N> field) noexcept : field_(field) {}

    [[nodiscard]] std::string_view text() const noexcept { return infra::text_until_nul(field_); }

    void assign(std::string_view text) noexcept {
        const std::size_t n = std::min(text.size(), N - 1);
        std::memcpy(field_.data(), text.data(), n);
        field_[n] = B{};
    }

private:
    std::span<B, N> field_;
};

}
