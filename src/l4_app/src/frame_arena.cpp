// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_arena.h"

namespace mister::app {

namespace {
constexpr std::size_t kPage = 4096;
}

Ex<void> FrameArena::reserve(std::size_t slots, std::size_t slot_bytes) noexcept {
    release();
    if (slots == 0 || slot_bytes == 0)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    const std::size_t stripe = (slot_bytes + kPage - 1) / kPage * kPage;

    const Ex<void> mapped = map_.resize(slots * stripe);
    if (!mapped)
        return mapped;
    slots_ = slots;
    slot_bytes_ = stripe;
    return {};
}

void FrameArena::release() noexcept {
    map_.release();
    slots_ = 0;
    slot_bytes_ = 0;
}

std::span<std::byte> FrameArena::stripe(std::size_t i) const noexcept {
    if (i >= slots_)
        return {};
    return map_.view().subspan(i * slot_bytes_, slot_bytes_);
}

std::span<std::byte> FrameArena::stripe(xthread::PoolSlot slot) const noexcept {
    if (!slot.within(xthread::PoolSize{slots_}))
        return {};
    return slot.subspan_of(map_.view(), xthread::SlotStride{slot_bytes_});
}

}
