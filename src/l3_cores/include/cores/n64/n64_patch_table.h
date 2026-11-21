// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/seat.h"

namespace mister::cores::n64 {

inline constexpr std::size_t kMaxPatches = 256;

class PatchTable {
    TASTY_SEAT_EXEMPT(component);

public:
    struct Entry {
        std::uint32_t address = 0;
        std::uint32_t diff = 0;
        bool turbo_core_only = false;
        friend constexpr bool operator==(const Entry&, const Entry&) = default;
    };

    constexpr bool push(const Entry& e) noexcept {
        if (count_ >= kMaxPatches) {
            ++dropped_;
            return false;
        }
        entries_[count_++] = e;
        return true;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }

    [[nodiscard]] constexpr std::size_t dropped() const noexcept { return dropped_; }

    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }

    [[nodiscard]] constexpr const Entry& operator[](std::size_t i) const noexcept {
        return entries_[i];
    }

    [[nodiscard]] constexpr std::span<const Entry> view() const noexcept {
        return std::span<const Entry>{entries_.data(), count_};
    }

    [[nodiscard]] constexpr std::size_t applicable() const noexcept {
        std::size_t n = 0;
        while (n < count_ && entries_[n].diff != 0)
            ++n;
        return n;
    }

private:
    std::array<Entry, kMaxPatches> entries_{};
    std::size_t count_ = 0;
    std::size_t dropped_ = 0;
};

}
