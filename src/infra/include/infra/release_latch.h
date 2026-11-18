// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "infra/kick_policy.h"
#include "infra/opt_ref.h"
#include "infra/release_gen.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::xthread {

template <class Kind, KickPolicy Kick = EventFdKick>
class BasicReleaseLatch {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(std::is_enum_v<Kind>, "Kind is an enum ordered weakest..strongest");
    static_assert(sizeof(Kind) == 1, "the kind packs into the ask word's low byte");
    static_assert(
        std::is_unsigned_v<std::underlying_type_t<Kind>>,
        "a signed kind would sign-extend into the generation bits"
    );
    static_assert(std::atomic<ReleaseGen>::is_always_lock_free);

public:
    struct Ticket {
        ReleaseGen gen;
    };

    struct Taken {
        ReleaseGen gen;
        Kind kind;
    };

    explicit BasicReleaseLatch(
        BasicWakeFlag<Kick>& worker_wake,
        infra::OptRef<BasicWakeFlag<Kick>> asker_wake = {}
    ) noexcept
        : worker_wake_(worker_wake),
          asker_wake_(asker_wake) {}

    Ticket ask(Kind k) noexcept {
        if (outstanding() && handed_ != last_asked_ && !(k > last_kind_)) {
            return Ticket{last_asked_};
        }
        last_asked_ = last_asked_.next();
        last_kind_ = k;
        ask_.store(Ask::of(last_asked_, k), std::memory_order_release);
        worker_wake_.kick_if_armed();
        return Ticket{last_asked_};
    }

    void handed_over() noexcept { handed_ = last_asked_; }

    [[nodiscard]] bool released(Ticket t) const noexcept {
        return done_.load(std::memory_order_acquire).covers(t.gen);
    }

    [[nodiscard]] bool outstanding() const noexcept {
        return !last_asked_.is_none() && !released(Ticket{last_asked_});
    }

    [[nodiscard]] std::optional<Taken> take() noexcept {
        const Ask w = ask_.load(std::memory_order_acquire);
        if (w.gen() == taken_)
            return std::nullopt;
        taken_ = w.gen();
        return Taken{w.gen(), w.kind()};
    }

    void done(Taken t) noexcept {
        if (answer(t.gen) && asker_wake_)
            asker_wake_->kick_if_armed();
    }

    void answer_through(ReleaseGen g) noexcept {
        if (g.covers(taken_))
            taken_ = g;
        if (answer(g) && asker_wake_)
            asker_wake_->kick_if_armed();
    }

    [[nodiscard]] bool pending() const noexcept {
        return ask_.load(std::memory_order_acquire).gen() != taken_;
    }

    [[nodiscard]] ReleaseGen asked() const noexcept {
        return ask_.load(std::memory_order_acquire).gen();
    }

private:
    struct Ask {
        std::uint32_t v = 0;

        [[nodiscard]] static constexpr Ask of(ReleaseGen g, Kind k) noexcept {
            return Ask{(g.v << (sizeof(Kind) * CHAR_BIT)) | static_cast<std::uint32_t>(k)};
        }

        [[nodiscard]] constexpr ReleaseGen gen() const noexcept {
            return ReleaseGen{v >> (sizeof(Kind) * CHAR_BIT)};
        }

        [[nodiscard]] constexpr Kind kind() const noexcept { return static_cast<Kind>(v); }

        friend constexpr bool operator==(Ask, Ask) = default;
    };

    static_assert(std::atomic<Ask>::is_always_lock_free);

    bool answer(ReleaseGen g) noexcept {
        if (done_.load(std::memory_order_relaxed).covers(g))
            return false;
        done_.store(g, std::memory_order_release);
        return true;
    }

    enum : std::size_t { kCacheLineBytes = 64 };

    std::atomic<Ask> ask_{};
    alignas(kCacheLineBytes) std::atomic<ReleaseGen> done_{};
    ReleaseGen taken_{};
    ReleaseGen last_asked_{};
    ReleaseGen handed_{};
    Kind last_kind_{};
    BasicWakeFlag<Kick>& worker_wake_;
    infra::OptRef<BasicWakeFlag<Kick>> asker_wake_;
};

}
