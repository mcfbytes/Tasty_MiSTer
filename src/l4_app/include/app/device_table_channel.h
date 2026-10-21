// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/slot_pool.h"
#include "infra/spsc_ring.h"
#include "svc/device_table.h"
#include "infra/seat.h"

namespace mister::app {

inline constexpr std::size_t kTableLeases = 2;
inline constexpr std::size_t kTableHandoffDepth = 4;
static_assert(kTableLeases < kTableHandoffDepth);

struct TableHandoff {
    TASTY_SEAT_EXEMPT(component);
    std::uint8_t lease = 0;
};

class DeviceTableChannel {
    TASTY_SEAT_MEDIATOR(Diag, Input);
    using Pool = xthread::SlotPool<svc::DeviceTable, kTableLeases>;

public:
    DeviceTableChannel() noexcept = default;
    DeviceTableChannel(const DeviceTableChannel&) = delete;
    DeviceTableChannel& operator=(const DeviceTableChannel&) = delete;

    class Sender {
        TASTY_SEAT_EXEMPT(component);

    public:
        explicit Sender(DeviceTableChannel& ch) noexcept
            : ch_(ch),
              live_(ch.mint_(kSenderMinted)) {}

        Sender(const Sender&) = delete;
        Sender& operator=(const Sender&) = delete;

        void reap() noexcept {
            const std::uint32_t taken = ch_.taken_.load(std::memory_order_acquire);
            for (std::size_t i = 0; i < kTableLeases; ++i) {
                if (!leases_[i] || static_cast<std::int32_t>(taken - lease_seq_[i]) < 0)
                    continue;
                *leases_[i] = svc::DeviceTable{};
                leases_[i].reset();
            }
        }

        [[nodiscard]] bool ready() noexcept {
            reap();
            return live_ && ch_.pool_.available() != 0;
        }

        [[nodiscard]] Ex<void> send(svc::DeviceTable& t) noexcept {
            reap();
            if (!live_)
                return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
            auto lease = ch_.pool_.acquire();
            if (!lease)
                return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
            std::swap(*lease, t);
            const std::uint8_t idx = ch_.pool_.index_of(lease.get());
            if (!ch_.ring_.push(TableHandoff{idx})) {
                std::swap(*lease, t);
                return std::unexpected(Error{Errc::would_block, ERR_SITE(), 1});
            }
            ++sent_;
            lease_seq_[idx] = sent_;
            leases_[idx] = std::move(lease);
            return {};
        }

        [[nodiscard]] std::size_t out() const noexcept {
            return kTableLeases - ch_.pool_.available();
        }

    private:
        DeviceTableChannel& ch_;
        bool live_;
        Pool::Lease leases_[kTableLeases]{};
        std::uint32_t lease_seq_[kTableLeases]{};
        std::uint32_t sent_ = 0;
    };

    class Receiver {
        TASTY_SEAT_EXEMPT(component);

    public:
        explicit Receiver(DeviceTableChannel& ch) noexcept
            : ch_(ch),
              live_(ch.mint_(kReceiverMinted)) {}

        Receiver(const Receiver&) = delete;
        Receiver& operator=(const Receiver&) = delete;

        [[nodiscard]] infra::OptRef<svc::DeviceTable> next() noexcept {
            if (!live_)
                return {};
            const auto h = ch_.ring_.pop();
            if (!h)
                return {};

            if (h->lease >= kTableLeases) {
                fatal(Error{Errc::slot_range, ERR_SITE(), h->lease}, "DeviceTableChannel::next");
            }
            return infra::OptRef<svc::DeviceTable>{ch_.pool_.at(h->lease)};
        }

        void taken() noexcept {
            ++taken_;
            ch_.taken_.store(taken_, std::memory_order_release);
        }

    private:
        DeviceTableChannel& ch_;
        bool live_;
        std::uint32_t taken_ = 0;
    };

private:
    static constexpr std::uint8_t kSenderMinted = 1u << 0;
    static constexpr std::uint8_t kReceiverMinted = 1u << 1;

    [[nodiscard]] bool mint_(std::uint8_t bit) noexcept {
        return (minted_.fetch_or(bit, std::memory_order_relaxed) & bit) == 0u;
    }

    xthread::SpscRing<TableHandoff, kTableHandoffDepth> ring_{};
    Pool pool_{};
    std::atomic<std::uint32_t> taken_{0};
    std::atomic<std::uint8_t> minted_{0};
};

}
