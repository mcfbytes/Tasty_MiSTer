// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "infra/seat.h"
#include "os/mmio_region.h"

namespace mister::hal {
template <class Layout>
class RegisterWindow;
}

namespace mister::hal::reg {

class Block;

class Lend {
    TASTY_SEAT_EXEMPT(component);

public:
    Lend() = default;

    [[nodiscard]] static Lend of(const os::MmioRegion& pages, std::size_t lead) noexcept {
        const std::size_t len = pages.length() >= lead ? pages.length() - lead : 0;
        return Lend(static_cast<volatile std::byte*>(pages.base()) + lead, len);
    }

    [[nodiscard]] static Lend over(std::span<std::uint32_t> words) noexcept {
        if (words.empty())
            return Lend{};
        return Lend(reinterpret_cast<volatile std::byte*>(words.data()), words.size_bytes());
    }

    [[nodiscard]] bool lent() const noexcept { return base_ != nullptr; }

    [[nodiscard]] std::size_t size() const noexcept { return len_; }

    [[nodiscard]] Lend first(std::size_t n) const noexcept {
        return Lend(base_, n < len_ ? n : len_);
    }

private:
    friend class Block;
    template <class Layout>
    friend class hal::RegisterWindow;

    Lend(volatile std::byte* base, std::size_t len) noexcept : base_(base), len_(len) {}

    volatile std::byte* base_ = nullptr;
    std::size_t len_ = 0;
};

static_assert(std::is_trivially_copyable_v<Lend>);

}
