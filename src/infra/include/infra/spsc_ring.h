// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

#include "infra/seat.h"

namespace mister::xthread {

template <class I>
concept RingIndex = std::unsigned_integral<I>
    && !std::same_as<I, bool>
    && sizeof(I) <= sizeof(std::uint32_t);

template <class T, std::size_t N, class Index = std::uint32_t>
class SpscRing {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(
        std::is_trivially_copyable_v<T>,
        "a ring payload carries NO ownership: a pointer here would run "
        "a destructor on whichever seat popped it. Most are small PODs "
        "keyed into pre-sized tables; a payload whose table cannot "
        "outlive the wake that reads it rides BY VALUE instead, and "
        "prices its own footprint."
    );
    static_assert((N & (N - 1)) == 0, "power-of-two capacity");
    static_assert(
        RingIndex<Index>,
        "the index is an unsigned word no wider than its storage: wrap is "
        "defined, and the 32-bit atomic holds it."
    );
    static_assert(
        N <= std::numeric_limits<Index>::max(),
        "occupancy must be representable at this index width"
    );

public:
    using index_type = Index;

    class Producer {
        TASTY_SEAT_EXEMPT(component);

    public:
        Producer() noexcept = default;

        Producer(Producer&& o) noexcept : ring_(std::exchange(o.ring_, nullptr)) {}

        Producer& operator=(Producer&& o) noexcept {
            if (this != &o)
                ring_ = std::exchange(o.ring_, nullptr);
            return *this;
        }

        Producer(const Producer&) = delete;
        Producer& operator=(const Producer&) = delete;

        [[nodiscard]] explicit operator bool() const noexcept { return ring_ != nullptr; }

        [[nodiscard]] bool push(const T& v) noexcept { return ring_->push(v); }

        [[nodiscard]] std::size_t size() const noexcept { return ring_->size(); }

        [[nodiscard]] Index pushed() const noexcept { return ring_->pushed(); }

        [[nodiscard]] Index popped() const noexcept { return ring_->popped(); }

    private:
        friend class SpscRing;

        explicit Producer(SpscRing* r) noexcept : ring_(r) {}

        SpscRing* ring_ = nullptr;
    };

    class Consumer {
        TASTY_SEAT_EXEMPT(component);

    public:
        Consumer() noexcept = default;

        Consumer(Consumer&& o) noexcept : ring_(std::exchange(o.ring_, nullptr)) {}

        Consumer& operator=(Consumer&& o) noexcept {
            if (this != &o)
                ring_ = std::exchange(o.ring_, nullptr);
            return *this;
        }

        Consumer(const Consumer&) = delete;
        Consumer& operator=(const Consumer&) = delete;

        [[nodiscard]] explicit operator bool() const noexcept { return ring_ != nullptr; }

        [[nodiscard]] std::optional<T> pop() noexcept { return ring_->pop(); }

        [[nodiscard]] std::size_t size() const noexcept { return ring_->size(); }

        [[nodiscard]] Index pushed() const noexcept { return ring_->pushed(); }

        [[nodiscard]] Index popped() const noexcept { return ring_->popped(); }

    private:
        friend class SpscRing;

        explicit Consumer(SpscRing* r) noexcept : ring_(r) {}

        SpscRing* ring_ = nullptr;
    };

    SpscRing() = default;

    explicit SpscRing(Index at) noexcept : head_(at), tail_(at) {}

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    [[nodiscard]] Producer producer() noexcept {
        return mint_(kProducerMinted) ? Producer{this} : Producer{};
    }

    [[nodiscard]] Consumer consumer() noexcept {
        return mint_(kConsumerMinted) ? Consumer{this} : Consumer{};
    }

    bool push(const T& v) noexcept {
        const Index head = static_cast<Index>(head_.load(std::memory_order_relaxed));
        const Index tail = static_cast<Index>(tail_.load(std::memory_order_acquire));
        if (static_cast<Index>(head - tail) >= N)
            return false;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
        slots_[head & (N - 1)] = v;
#pragma GCC diagnostic pop
        const std::uint32_t next = static_cast<Index>(head + 1);
        head_.store(next, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() noexcept {
        const Index tail = static_cast<Index>(tail_.load(std::memory_order_relaxed));
        const Index head = static_cast<Index>(head_.load(std::memory_order_acquire));
        if (head == tail)
            return std::nullopt;
        T v = slots_[tail & (N - 1)];
        const std::uint32_t next = static_cast<Index>(tail + 1);
        tail_.store(next, std::memory_order_release);
        return v;
    }

    std::size_t size() const noexcept { return static_cast<Index>(pushed() - popped()); }

    Index pushed() const noexcept {
        return static_cast<Index>(head_.load(std::memory_order_acquire));
    }

    Index popped() const noexcept {
        return static_cast<Index>(tail_.load(std::memory_order_acquire));
    }

private:
    static constexpr std::uint8_t kProducerMinted = 1;
    static constexpr std::uint8_t kConsumerMinted = 2;

    [[nodiscard]] bool mint_(std::uint8_t side) noexcept {
        if ((minted_ & side) != 0)
            return false;
        minted_ = static_cast<std::uint8_t>(minted_ | side);
        return true;
    }

    alignas(64) std::atomic<std::uint32_t> head_{0};
    std::uint8_t minted_ = 0;
    alignas(64) std::atomic<std::uint32_t> tail_{0};
    alignas(64) T slots_[N];
};

}
