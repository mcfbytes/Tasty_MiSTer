// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <concepts>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

#include "infra/loan_channel.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::app {

template <
    class Body,
    std::size_t N,
    SeatTag ProducerSeat,
    SeatTag ConsumerSeat,
    class Position = std::uint32_t>
class RingSlab {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(std::is_trivially_copyable_v<Body>, "a slot is recycled in place");
    static_assert(
        (ProducerSeat == SeatTag::RT) != (ConsumerSeat == SeatTag::RT),
        "exactly one side is T-RT: both ends poll, and only a T-RT pair may build Polled"
    );
    static_assert(
        std::unsigned_integral<Position> && !std::same_as<Position, bool>,
        "Position is the ring's own unsigned index type: the release test wraps at its width"
    );

public:
    struct Cell {
        TASTY_SEAT_MEDIATOR(Any, Any);
        Body body{};
        Position at = 0;
    };

private:
    using Channel = xthread::LoanChannel<Cell, N, ProducerSeat, ConsumerSeat>;
    using Loan = typename Channel::Loan;
    using Job = typename Channel::Job;

public:
    using Index = std::uint8_t;
    static constexpr std::size_t kCapacity = N;

    class Producer {
        TASTY_SEAT_EXEMPT(component);

    public:
        Producer(typename Channel::Owner owner, std::uint8_t live) noexcept
            : owner_(std::move(owner)) {
            const SeatScope wiring{ProducerSeat};
            live_ok_ = owner_.set_live(live);
        }

        Producer(const Producer&) = delete;
        Producer& operator=(const Producer&) = delete;

        ~Producer() {
            const SeatScope stands_in{ProducerSeat};
            for (auto& l : staged_)
                l = Loan{};
        }

        [[nodiscard]] bool live_ok() const noexcept { return live_ok_; }

        [[nodiscard]] std::optional<Index> reap() noexcept {
            const Loan l = owner_.reap();
            if (!l)
                return std::nullopt;
            return owner_.index_of(l);
        }

        [[nodiscard]] std::optional<Index> stage() noexcept {
            Loan l = owner_.acquire();
            if (!l)
                return std::nullopt;
            const Index i = owner_.index_of(l);
            staged_[i] = std::move(l);
            return i;
        }

        [[nodiscard]] infra::OptRef<Body> staged(Index i) noexcept {
            seat_assert<ProducerSeat>(ERR_SITE(), "RingSlab::staged off the producer seat");
            if (i >= N || !staged_[i])
                return {};
            return infra::OptRef<Body>{staged_[i]->body};
        }

        void unstage(Index i) noexcept {
            seat_assert<ProducerSeat>(ERR_SITE(), "RingSlab::unstage off the producer seat");
            if (i < N)
                staged_[i] = Loan{};
        }

        void send_staged(std::same_as<Position> auto at) noexcept {
            for (auto& l : staged_) {
                if (!l)
                    continue;
                l->at = at;
                owner_.send(std::move(l));
            }
        }

        [[nodiscard]] std::size_t free_now() const noexcept {
            const auto c = owner_.census();
            return std::size_t(c.idle) + c.inbound;
        }

    private:
        typename Channel::Owner owner_;
        Loan staged_[N]{};
        bool live_ok_ = false;
    };

    class Consumer {
        TASTY_SEAT_EXEMPT(component);

    public:
        explicit Consumer(typename Channel::Worker worker) noexcept : worker_(std::move(worker)) {}

        Consumer(const Consumer&) = delete;
        Consumer& operator=(const Consumer&) = delete;

        ~Consumer() {
            const SeatScope stands_in{ConsumerSeat};
            for (auto& j : hand_)
                j = Job{};
        }

        void release_before(std::same_as<Position> auto q) noexcept {
            seat_assert<ConsumerSeat>(ERR_SITE(), "RingSlab::release_before off the consumer seat");
            if (held_ == 0)
                return;
            for (auto& j : hand_) {
                if (j && before_(j->at, q)) {
                    j = Job{};
                    --held_;
                }
            }
        }

        void take_sent() noexcept {
            for (std::size_t n = 0; n < N; ++n) {
                Job j = worker_.take();
                if (!j)
                    return;
                const Index i = j.index();
                hand_[i] = std::move(j);
                ++held_;
            }
        }

        [[nodiscard]] infra::OptRef<const Body> held(Index i) const noexcept {
            seat_assert<ConsumerSeat>(ERR_SITE(), "RingSlab::held off the consumer seat");
            if (i >= N || !hand_[i])
                return {};
            return infra::OptRef<const Body>{hand_[i]->body};
        }

    private:
        [[nodiscard]] static constexpr bool before_(Position at, Position q) noexcept {
            return static_cast<std::make_signed_t<Position>>(static_cast<Position>(q - at)) > 0;
        }

        typename Channel::Worker worker_;
        Job hand_[N]{};
        std::size_t held_ = 0;
    };

    explicit RingSlab(std::uint8_t live) noexcept
        : ch_(xthread::Polled{}),
          producer_(ch_.owner(), live),
          consumer_(ch_.worker()) {}

    RingSlab(const RingSlab&) = delete;
    RingSlab& operator=(const RingSlab&) = delete;
    RingSlab(RingSlab&&) = delete;
    RingSlab& operator=(RingSlab&&) = delete;
    ~RingSlab() = default;

    [[nodiscard]] Producer& producer() noexcept { return producer_; }

    [[nodiscard]] const Producer& producer() const noexcept { return producer_; }

    [[nodiscard]] Consumer& consumer() noexcept { return consumer_; }

    [[nodiscard]] const Consumer& consumer() const noexcept { return consumer_; }

private:
    Channel ch_;
    Producer producer_;
    Consumer consumer_;
};

}
