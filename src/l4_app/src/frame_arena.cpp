// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_arena.h"

#include <sys/mman.h>

#include <cerrno>
#include <cstdint>

namespace mister::app {

namespace {
constexpr std::size_t kPage = 4096;
}

Ex<void> FrameArena::reserve(std::size_t slots, std::size_t slot_bytes) noexcept {
    release();
    if (slots == 0 || slot_bytes == 0)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    const std::size_t stripe = (slot_bytes + kPage - 1) / kPage * kPage;

    void* p =
        ::mmap(nullptr, slots * stripe, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return std::unexpected(
            Error{Errc::mmap_failed, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    base_ = static_cast<std::byte*>(p);
    slots_ = slots;
    slot_bytes_ = stripe;
    return {};
}

void FrameArena::release() noexcept {
    if (base_ != nullptr) (void)::munmap(base_, slots_ * slot_bytes_);
    base_ = nullptr;
    slots_ = 0;
    slot_bytes_ = 0;
}

std::span<std::byte> FrameArena::stripe(std::size_t i) const noexcept {
    if (base_ == nullptr || i >= slots_) return {};
    return {base_ + i * slot_bytes_, slot_bytes_};
}

}  // namespace mister::app
