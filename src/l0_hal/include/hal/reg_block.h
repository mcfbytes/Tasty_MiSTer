// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

#include "infra/seat.h"
#include "hal/reg.h"
#include "hal/reg_field.h"
#include "hal/reg_lend.h"
#include "os/mmio_region.h"

namespace mister::hal::reg {

class Block {
    TASTY_SEAT_EXEMPT(component);

public:
    Block() = default;

    Block(Block&& o) noexcept
        : base_(std::exchange(o.base_, nullptr)),
          len_(std::exchange(o.len_, 0)) {}

    Block& operator=(Block&& o) noexcept {
        if (this != &o) {
            base_ = std::exchange(o.base_, nullptr);
            len_ = std::exchange(o.len_, 0);
        }
        return *this;
    }

    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    ~Block() = default;

    [[nodiscard]] static Block of(const os::MmioRegion& pages, std::size_t lead) noexcept {
        return redeem(Lend::of(pages, lead));
    }

    [[nodiscard]] static Block over(std::span<std::uint32_t> words) noexcept {
        return redeem(Lend::over(words));
    }

    [[nodiscard]] static Block redeem(Lend l) noexcept { return Block(l.base_, l.len_); }

    [[nodiscard]] bool lent() const noexcept { return base_ != nullptr; }

    [[nodiscard]] std::size_t size() const noexcept { return len_; }

    [[nodiscard]] Block first(std::size_t n) && noexcept {
        const std::size_t len = n < len_ ? n : len_;
        len_ = 0;
        return Block(std::exchange(base_, nullptr), len);
    }

    template <Access A>
    [[nodiscard]] std::optional<Reg<A>> at(std::uint32_t byte_off) const noexcept {
        if (base_ == nullptr
            || (byte_off & 3u) != 0u
            || len_ < sizeof(std::uint32_t)
            || static_cast<std::size_t>(byte_off) > len_ - sizeof(std::uint32_t)) {
            return std::nullopt;
        }
        return Reg<A>(reinterpret_cast<volatile std::uint32_t*>(base_ + byte_off));
    }

    template <Access A, std::uint32_t Off>
    [[nodiscard]] std::optional<Reg<A>> at(Field<A, Off>) const noexcept {
        return at<A>(Off);
    }

private:
    template <class Layout>
    friend class hal::RegisterWindow;

    Block(volatile std::byte* base, std::size_t len) noexcept : base_(base), len_(len) {}

    volatile std::byte* base_ = nullptr;
    std::size_t len_ = 0;
};

static_assert(!std::is_copy_constructible_v<Block> && !std::is_copy_assignable_v<Block>);
static_assert(std::is_nothrow_move_constructible_v<Block>);

}
