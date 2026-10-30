// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

#include "infra/counter.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "infra/spsc_ring.h"

namespace mister::xthread {

template <
    infra::MessageSum Sum,
    std::size_t N,
    SeatTag ProducerSeat,
    SeatTag ConsumerSeat,
    class Mint = void>
class CountedLane {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(
        ProducerSeat != SeatTag::Unbound && ConsumerSeat != SeatTag::Unbound,
        "a lane names both of its seats: Any would switch off both seat checks. "
        ""
    );
    static_assert(ProducerSeat != ConsumerSeat, "a lane crosses between two different seats");
    static_assert(infra::alternatives_are_total<Sum>(), "a lane carries a total message sum");

    using Ring = SpscRing<Sum, N>;

public:
    using Kind = typename Sum::Kind;
    static constexpr std::size_t kCapacity = N;
    static constexpr std::size_t kKinds = infra::ordinal(Kind::kCount);
    static constexpr SeatTag kProducerSeat = ProducerSeat;
    static constexpr SeatTag kConsumerSeat = ConsumerSeat;

    struct Losses {
        TASTY_SEAT_EXEMPT(component);
        std::array<std::uint32_t, kKinds> per_kind{};

        [[nodiscard]] constexpr std::uint32_t of(Kind k) const noexcept {
            return per_kind[infra::ordinal(k)];
        }

        [[nodiscard]] constexpr std::uint32_t total() const noexcept {
            std::uint32_t sum = 0;
            for (const std::uint32_t n : per_kind)
                sum = (sum > kSaturated - n) ? kSaturated : sum + n;
            return sum;
        }
    };

    class Producer {
        TASTY_SEAT_EXEMPT(component);

    public:
        template <class A, class... HeadArgs>
            requires infra::AlternativeOf<A, Sum>
        bool push(const A& alt, const HeadArgs&... head) noexcept {
            seat_assert<ProducerSeat>(ERR_SITE(), "CountedLane::push off the producer seat");
            return lane_->offer_(alt, head...);
        }

        [[nodiscard]] std::size_t size() const noexcept { return lane_->ring_.size(); }

    private:
        friend class CountedLane;

        explicit Producer(CountedLane& l) noexcept : lane_(&l) {}

        CountedLane* lane_;
    };

    class Consumer {
        TASTY_SEAT_EXEMPT(component);

    public:
        Consumer() noexcept = default;
        Consumer(Consumer&&) noexcept = default;
        Consumer& operator=(Consumer&&) noexcept = default;
        Consumer(const Consumer&) = delete;
        Consumer& operator=(const Consumer&) = delete;

        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(rx_); }

        [[nodiscard]] std::optional<Sum> pop() noexcept {
            seat_assert<ConsumerSeat>(ERR_SITE(), "CountedLane::pop off the consumer seat");
            return rx_.pop();
        }

        [[nodiscard]] std::size_t size() const noexcept { return rx_.size(); }

    private:
        friend class CountedLane;

        explicit Consumer(typename Ring::Consumer rx) noexcept : rx_(std::move(rx)) {}

        typename Ring::Consumer rx_;
    };

    CountedLane() = default;
    CountedLane(const CountedLane&) = delete;
    CountedLane& operator=(const CountedLane&) = delete;

    [[nodiscard]] Producer producer() noexcept { return Producer{*this}; }

    [[nodiscard]] Consumer consumer() noexcept { return Consumer{ring_.consumer()}; }

    [[nodiscard]] Losses losses() const noexcept {
        Losses s;
        for (std::size_t i = 0; i < kKinds; ++i)
            s.per_kind[i] = loss_[i].get();
        return s;
    }

    [[nodiscard]] std::size_t depth() const noexcept { return ring_.size(); }

private:
    template <class A, class... HeadArgs>
    [[nodiscard]] static Sum build_(const A& alt, const HeadArgs&... head) noexcept {
        if constexpr (std::is_void_v<Mint>)
            return infra::make<Sum>(alt, head...);
        else
            return infra::make<Sum>(alt, Mint::template head<A>(head...));
    }

    template <class A, class... HeadArgs>
    bool offer_(const A& alt, const HeadArgs&... head) noexcept {
        static_assert(infra::ordinal(A::kKind) < kKinds);
        if (ring_.push(build_(alt, head...)))
            return true;
        Counter& lost = loss_[infra::ordinal(A::kKind)];
        if (lost.get() != kSaturated)
            lost.add();
        return false;
    }

    static constexpr std::uint32_t kSaturated = 0xFFFFFFFFu;

    Ring ring_;
    Counter loss_[kKinds];
};

}
