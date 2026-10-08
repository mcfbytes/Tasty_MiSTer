// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "infra/seat.h"

namespace mister::xthread {

template <class T>
class SeqCell {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(std::is_trivially_copyable_v<T>,
                  "the body crosses as its bytes, in words: a cell "
                  "payload is a POD, never an owning type");
    static_assert(std::is_nothrow_default_constructible_v<T>);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

public:
    using value_type = T;
    using Gen = std::uint32_t;

    SeqCell() = default;
    SeqCell(const SeqCell&) = delete;
    SeqCell& operator=(const SeqCell&) = delete;
    SeqCell(SeqCell&&) = delete;
    SeqCell& operator=(SeqCell&&) = delete;

    void write(Gen g, const T& v) noexcept {
        gen_.store(0, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        const auto* src = reinterpret_cast<const unsigned char*>(&v);
        std::atomic<std::uint32_t>* const words = body_;
        for (std::size_t i = 0; i < kFull; ++i) {
            std::uint32_t w;
            std::memcpy(&w, src + i * kWord, kWord);
            words[i].store(w, std::memory_order_relaxed);
        }
        if constexpr (kTail != 0) {
            std::uint32_t w = 0;
            std::memcpy(&w, src + kFull * kWord, kTail);
            words[kFull].store(w, std::memory_order_relaxed);
        }
        gen_.store(g, std::memory_order_release);
    }

    void clear() noexcept { gen_.store(0, std::memory_order_relaxed); }

    [[nodiscard]] Gen try_read(T& dst) const noexcept {
        const Gen g1 = gen_.load(std::memory_order_acquire);
        if (g1 == 0) return 0;
        auto* out = reinterpret_cast<unsigned char*>(&dst);
        const std::atomic<std::uint32_t>* const words = body_;
        for (std::size_t i = 0; i < kFull; ++i) {
            const std::uint32_t w = words[i].load(std::memory_order_relaxed);
            std::memcpy(out + i * kWord, &w, kWord);
        }
        if constexpr (kTail != 0) {
            const std::uint32_t w = words[kFull].load(std::memory_order_relaxed);
            std::memcpy(out + kFull * kWord, &w, kTail);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        return gen_.load(std::memory_order_relaxed) == g1 ? g1 : 0;
    }

    [[nodiscard]] Gen generation() const noexcept { return gen_.load(std::memory_order_acquire); }

private:
    static constexpr std::size_t kWord = sizeof(std::uint32_t);
    static constexpr std::size_t kFull = sizeof(T) / kWord;
    static constexpr std::size_t kTail = sizeof(T) % kWord;

    std::atomic<Gen> gen_{0};
    std::atomic<std::uint32_t> body_[kFull + (kTail != 0 ? 1 : 0)]{};
};

}  // namespace mister::xthread
