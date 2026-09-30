// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"

namespace mister::hal {

class FpgaMemory;

class DmaRing {
public:
    static Ex<DmaRing> create(FpgaMemory& mem, std::size_t off, std::size_t len);

    static constexpr std::size_t kHeaderBytes = 8;
    static constexpr std::size_t kMinCapacity = 16;

    std::span<std::byte> reserve(std::size_t n);
    void publish(std::size_t n);
    std::span<const std::byte> peek();
    void release(std::size_t n);

    std::size_t capacity() const noexcept { return mask_ + 1u; }
    std::size_t used() const;
    std::size_t space() const;

private:
    DmaRing() = default;
    void check_live() const;

    FpgaMemory* mem_ = nullptr;
    std::size_t data_off_ = 0;
    std::size_t mask_ = 0;
    std::size_t head_word_ = 0;
    std::size_t tail_word_ = 0;
    std::size_t pending_ = 0;
    std::uint32_t generation_ = 0;
};

}  // namespace mister::hal
