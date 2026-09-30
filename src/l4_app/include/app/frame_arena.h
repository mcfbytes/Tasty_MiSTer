// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::app {

class FrameArena {
    TASTY_SEAT_EXEMPT(component);

public:
    FrameArena() noexcept = default;
    ~FrameArena() { release(); }
    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;
    FrameArena(FrameArena&&) = delete;
    FrameArena& operator=(FrameArena&&) = delete;

    [[nodiscard]] Ex<void> reserve(std::size_t slots, std::size_t slot_bytes) noexcept;
    void release() noexcept;

    [[nodiscard]] std::span<std::byte> stripe(std::size_t i) const noexcept;
    [[nodiscard]] std::size_t slot_bytes() const noexcept { return slot_bytes_; }
    [[nodiscard]] std::size_t bytes() const noexcept { return slots_ * slot_bytes_; }

private:
    std::byte* base_ = nullptr;
    std::size_t slots_ = 0;
    std::size_t slot_bytes_ = 0;
};

}  // namespace mister::app
