// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::xthread {

template <class T, SeatTag Owner = SeatTag::Unbound, unsigned Attempts = 2>
class Telemetry {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(std::is_trivially_copyable_v<T>,
                  "the body crosses as its bytes, in words: a cell "
                  "payload is a POD, never an owning type");
    static_assert(std::is_nothrow_default_constructible_v<T>);
    static_assert(Attempts >= 1, "a bounded read needs at least one attempt");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

public:
    using value_type = T;

    struct Sample {
        T value{};
        std::uint32_t generation = 0;
        explicit constexpr operator bool() const noexcept { return generation != 0; }
    };

    class Reader {
        TASTY_SEAT_EXEMPT(component);

    public:
        [[nodiscard]] std::optional<T> take_if_changed(const Telemetry& cell) noexcept {
            const std::uint32_t g = cell.generation();
            if (g == 0 || g == seen_) return std::nullopt;
            const Sample s = cell.sample();
            if (!s) return std::nullopt;
            seen_ = s.generation;
            return s.value;
        }

        [[nodiscard]] bool take_if_changed(const Telemetry& cell, T& dst) noexcept {
            const std::uint32_t g = cell.generation();
            if (g == 0 || g == seen_) return false;
            const std::uint32_t got = cell.sample_into(dst);
            if (got == 0) return false;
            seen_ = got;
            return true;
        }
        [[nodiscard]] std::uint32_t seen() const noexcept { return seen_; }
        void forget() noexcept { seen_ = 0; }

    private:
        std::uint32_t seen_ = 0;
    };

    Telemetry() = default;
    Telemetry(const Telemetry&) = delete;
    Telemetry& operator=(const Telemetry&) = delete;
    Telemetry(Telemetry&&) = delete;
    Telemetry& operator=(Telemetry&&) = delete;

    void publish(const T& v) noexcept {
        seat_assert<Owner>(ERR_SITE(), "Telemetry::publish off its owning seat");
        ++seq_;
        if (seq_ == 0) seq_ = 1;
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
        gen_.store(seq_, std::memory_order_release);
    }

    void invalidate() noexcept {
        seat_assert<Owner>(ERR_SITE(), "Telemetry::invalidate off its owning seat");
        gen_.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t sample_into(T& dst) const noexcept {
        for (unsigned attempt = 0; attempt < Attempts; ++attempt) {
            const std::uint32_t g1 = gen_.load(std::memory_order_acquire);
            if (g1 == 0) continue;
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
            if (gen_.load(std::memory_order_relaxed) == g1) return g1;
        }
        refused_.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    [[nodiscard]] Sample sample() const noexcept {
        Sample s;
        const std::uint32_t g = sample_into(s.value);
        if (g == 0) return Sample{};
        s.generation = g;
        return s;
    }

    [[nodiscard]] std::uint32_t generation() const noexcept {
        return gen_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint32_t refusals() const noexcept {
        return refused_.load(std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t kWord = sizeof(std::uint32_t);
    static constexpr std::size_t kFull = sizeof(T) / kWord;
    static constexpr std::size_t kTail = sizeof(T) % kWord;

    std::atomic<std::uint32_t> gen_{0};
    std::atomic<std::uint32_t> body_[kFull + (kTail != 0 ? 1 : 0)]{};
    std::uint32_t seq_ = 0;
    mutable std::atomic<std::uint32_t> refused_{0};
};

}  // namespace mister::xthread
