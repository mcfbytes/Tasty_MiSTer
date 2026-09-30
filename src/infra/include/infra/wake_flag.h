// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <concepts>
#include <cstdint>

#include "infra/error.h"
#include "infra/unique_fd.h"
#include "infra/seat.h"

namespace mister::xthread {

struct EventFdKick {
    static constexpr bool kModel = false;
};
struct ModelKick {
    static constexpr bool kModel = true;
};
template <class K>
concept KickPolicy = std::same_as<K, EventFdKick> || std::same_as<K, ModelKick>;

template <KickPolicy Kick = EventFdKick>
class BasicWakeFlag {
    TASTY_SEAT_MEDIATOR(Any, Any);
    using Seq = std::uint32_t;
    static_assert(std::atomic<Seq>::is_always_lock_free,
                  "request() is reachable from a signal handler; "
                  "[support.signal] admits only a lock-free atomic there");
    static_assert(static_cast<Seq>(0) - static_cast<Seq>(1) > 0,
                  "the counter must be UNSIGNED: wrap has to be defined, and "
                  "signed overflow is UB, not a large number");

public:
    BasicWakeFlag() = default;

    BasicWakeFlag(BasicWakeFlag&& o) noexcept
        : seq_(o.seq_.load(std::memory_order_relaxed)),
          armed_(o.armed_.load(std::memory_order_relaxed)),
          pending_(o.pending_.load(std::memory_order_relaxed)), fd_(std::move(o.fd_)) {}

    [[nodiscard]] Ex<void> open_fd() noexcept {
        if constexpr (Kick::kModel) {
            return {};
        } else {
            const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (fd < 0) {
                return std::unexpected(
                    Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
            }
            fd_.reset(fd);
            return {};
        }
    }

    [[nodiscard]] int fd() const noexcept { return fd_.get(); }

    void request() noexcept {
        seq_.fetch_add(1, std::memory_order_release);
        kick();
    }

    void kick() noexcept {
        if constexpr (Kick::kModel) {
            pending_.fetch_add(1, std::memory_order_release);
        } else {
            const int fd = fd_.get();
            if (fd < 0) return;
            const int saved = errno;
            const std::uint64_t one = 1;
            ssize_t n = 0;
            do {
                n = ::write(fd, &one, sizeof one);
            } while (n < 0 && errno == EINTR);
            (void)n;
            errno = saved;
        }
    }

    void arm() noexcept {
        if constexpr (kSeatChecksEnabled) {
            if (armed_.exchange(true, std::memory_order_seq_cst))
                fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "WakeFlag armed while armed");
        } else {
            armed_.store(true, std::memory_order_seq_cst);
        }
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void disarm() noexcept {
        if constexpr (kSeatChecksEnabled) {
            if (!armed_.exchange(false, std::memory_order_relaxed))
                fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "WakeFlag disarmed while disarmed");
        } else {
            armed_.store(false, std::memory_order_relaxed);
        }
    }

    void kick_if_armed() noexcept {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (armed_.load(std::memory_order_acquire)) kick();
    }

    [[nodiscard]] std::uint32_t pending() const noexcept
        requires(Kick::kModel)
    {
        return pending_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool ever_requested() const noexcept {
        return seq_.load(std::memory_order_acquire) != 0u;
    }

    class Cursor {
        TASTY_SEAT_EXEMPT(component);

    public:
        Cursor() = default;

        explicit Cursor(Seq seen) noexcept : seen_(seen) {}

        [[nodiscard]] bool take(const BasicWakeFlag& w) noexcept {
            const Seq now = w.seq_.load(std::memory_order_acquire);
            if (now == seen_) return false;
            seen_ = now;
            return true;
        }

    private:
        Seq seen_ = 0;
    };

    void drain() noexcept {
        if constexpr (Kick::kModel) {
            pending_.exchange(0, std::memory_order_acquire);
        } else {
            const int fd = fd_.get();
            if (fd < 0) return;
            std::uint64_t v = 0;
            ssize_t n = 0;
            do {
                n = ::read(fd, &v, sizeof v);
            } while (n < 0 && errno == EINTR);
            (void)n;
        }
    }

private:
    std::atomic<Seq> seq_{0};
    alignas(64) std::atomic<bool> armed_{false};
    std::atomic<std::uint32_t> pending_{0};
    UniqueFd fd_{};
};

using WakeFlag = BasicWakeFlag<EventFdKick>;

struct Polled {};

}  // namespace mister::xthread
