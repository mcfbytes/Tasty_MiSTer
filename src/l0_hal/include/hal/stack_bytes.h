// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

namespace mister::hal {

class StackBytes {
public:
    constexpr StackBytes() noexcept = default;
    static constexpr std::size_t kKib = 1024;

    [[nodiscard]] static constexpr StackBytes kib(std::size_t n) noexcept {
        return StackBytes{n * kKib};
    }

    [[nodiscard]] static constexpr StackBytes mib(std::size_t n) noexcept {
        return StackBytes{n * kKib * kKib};
    }

    [[nodiscard]] constexpr bool is_main() const noexcept { return bytes_ == 0; }

    [[nodiscard]] constexpr std::size_t bytes() const noexcept { return bytes_; }

    friend constexpr bool operator==(StackBytes, StackBytes) = default;

private:
    explicit constexpr StackBytes(std::size_t n) noexcept : bytes_(n) {}

    std::size_t bytes_ = 0;
};

}
