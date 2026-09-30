// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "infra/error.h"

namespace mister::hal {

class CompatibleBlob {
public:
    static constexpr std::size_t kCap = 512;

    CompatibleBlob() noexcept = default;

    [[nodiscard]] Ex<void> assign(std::span<const char> raw) noexcept;

    [[nodiscard]] bool contains(std::string_view s) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return len_; }
    [[nodiscard]] bool empty() const noexcept { return len_ == 0; }
    [[nodiscard]] std::span<const char> raw() const noexcept { return {data_.data(), len_}; }

private:
    std::array<char, kCap> data_{};
    std::size_t len_ = 0;
};

}  // namespace mister::hal
