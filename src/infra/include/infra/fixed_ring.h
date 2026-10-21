// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "infra/seat.h"

namespace mister::infra {

template <class T, std::size_t N>
class FixedRing {
    TASTY_SEAT_EXEMPT(component);
    static_assert(N > 0 && N <= 255, "indices are uint8_t");
    static_assert(std::is_trivially_copyable_v<T>);

public:
    bool push(const T& value) noexcept {
        if (count_ >= N)
            return false;
        slots_[tail_] = value;
        tail_ = static_cast<std::uint8_t>((tail_ + 1u) % N);
        ++count_;
        return true;
    }

    [[nodiscard]] std::optional<T> pop() noexcept {
        if (count_ == 0)
            return std::nullopt;
        std::optional<T> out{slots_[head_]};
        head_ = static_cast<std::uint8_t>((head_ + 1u) % N);
        --count_;
        return out;
    }

    void clear() noexcept { head_ = tail_ = count_ = 0; }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }

    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

    static constexpr std::size_t capacity() noexcept { return N; }

private:
    std::array<T, N> slots_{};
    std::uint8_t head_ = 0, tail_ = 0, count_ = 0;
};

}
