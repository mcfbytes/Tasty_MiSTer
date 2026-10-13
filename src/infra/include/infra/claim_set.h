// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <utility>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::infra {

struct ClaimSetCounts {
    std::uint32_t claimed = 0;
    std::uint32_t refused = 0;
    std::uint32_t returned = 0;
    std::uint32_t high_water = 0;
};

template <class Id, std::size_t N>
class ClaimSet {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(std::is_enum_v<Id>, "a claim is named by an enumerator");
    static_assert(N > 0 && N <= 64, "the held set is one 64-bit word");

public:
    class Claim {
        TASTY_SEAT_EXEMPT(component);

    public:
        Claim() noexcept = default;
        Claim(Claim&& o) noexcept : set_(std::exchange(o.set_, nullptr)), id_(o.id_) {}
        Claim& operator=(Claim&& o) noexcept {
            if (this != &o) {
                reset();
                set_ = std::exchange(o.set_, nullptr);
                id_ = o.id_;
            }
            return *this;
        }
        Claim(const Claim&) = delete;
        Claim& operator=(const Claim&) = delete;
        ~Claim() { reset(); }

        [[nodiscard]] explicit operator bool() const noexcept { return set_ != nullptr; }
        [[nodiscard]] Id id() const noexcept { return id_; }

        void reset() noexcept {
            if (set_ != nullptr) std::exchange(set_, nullptr)->give_back_(id_);
        }

    private:
        friend class ClaimSet;
        Claim(ClaimSet* set, Id id) noexcept : set_(set), id_(id) {}

        ClaimSet* set_ = nullptr;
        Id id_{};
    };

    ClaimSet() noexcept = default;

    ClaimSet(ClaimSet&&) = delete;
    ClaimSet& operator=(ClaimSet&&) = delete;
    ClaimSet(const ClaimSet&) = delete;
    ClaimSet& operator=(const ClaimSet&) = delete;

    ~ClaimSet() {
        assert(held_.load(std::memory_order_relaxed) == 0 && "ClaimSet destroyed with a claim out");
    }

    [[nodiscard]] Ex<Claim> claim(Id id) noexcept {
        const auto i = static_cast<std::size_t>(id);
        if (i >= N) {
            refused_.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(
                Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(i)});
        }
        const std::uint64_t bit = std::uint64_t{1} << i;
        std::uint64_t held = held_.load(std::memory_order_relaxed);
        for (;;) {
            if ((held & bit) != 0u) {
                refused_.fetch_add(1, std::memory_order_relaxed);
                return std::unexpected(
                    Error{Errc::busy, ERR_SITE(), static_cast<std::uint32_t>(i)});
            }
            if (held_.compare_exchange_weak(held, held | bit, std::memory_order_acquire,
                                            std::memory_order_relaxed))
                break;
        }
        const std::uint32_t n = held_n_.fetch_add(1, std::memory_order_relaxed) + 1;
        claimed_.fetch_add(1, std::memory_order_relaxed);
        std::uint32_t hw = high_water_.load(std::memory_order_relaxed);
        while (n > hw && !high_water_.compare_exchange_weak(hw, n, std::memory_order_relaxed)) {
        }
        return Claim{this, id};
    }

    [[nodiscard]] bool held(Id id) const noexcept {
        const auto i = static_cast<std::size_t>(id);
        return i < N && (held_.load(std::memory_order_relaxed) & (std::uint64_t{1} << i)) != 0u;
    }
    [[nodiscard]] std::size_t held_count() const noexcept {
        return held_n_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] ClaimSetCounts counts() const noexcept {
        return ClaimSetCounts{
            claimed_.load(std::memory_order_relaxed), refused_.load(std::memory_order_relaxed),
            returned_.load(std::memory_order_relaxed), high_water_.load(std::memory_order_relaxed)};
    }
    static constexpr std::size_t capacity() noexcept { return N; }

private:
    void give_back_(Id id) noexcept {
        const std::uint64_t bit = std::uint64_t{1} << static_cast<std::size_t>(id);
        if ((held_.fetch_and(~bit, std::memory_order_release) & bit) == 0u) return;
        held_n_.fetch_sub(1, std::memory_order_relaxed);
        returned_.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<std::uint64_t> held_{0};
    std::atomic<std::uint32_t> held_n_{0};
    std::atomic<std::uint32_t> claimed_{0};
    std::atomic<std::uint32_t> refused_{0};
    std::atomic<std::uint32_t> returned_{0};
    std::atomic<std::uint32_t> high_water_{0};
};

}  // namespace mister::infra
