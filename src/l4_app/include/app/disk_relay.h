// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include "app/disk_slot.h"
#include "app/path_text.h"
#include "cores/disk_port.h"
#include "cores/disk_servant.h"
#include "infra/counter.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/polled.h"
#include "infra/loan_channel.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "proto/spi_fio_queue.h"
#include "proto/types.h"
#include "svc/io_coworker.h"

namespace mister::app {

class DiskRelay final : public cores::IDiskPort, public svc::IIoCoworker {
    TASTY_SEAT_MEDIATOR(RT, Io);

public:
    using Chan = xthread::LoanChannel<DiskSlot, kDiskDepth, SeatTag::RT, SeatTag::Io>;
    using Job = Chan::Job;
    static constexpr std::uint8_t kUnits = 2;

    static constexpr std::uint32_t kMaxRoundWords = 1u + proto::kDiskActBytes / 2u;
    static_assert(kMaxRoundWords <= proto::tightest_round_budget_words());

    explicit DiskRelay(xthread::Polled, std::unique_ptr<cores::IDiskServant> servant) noexcept
        : io_{.servant = std::move(servant)},
          chan_(xthread::Polled{}) {}

    explicit DiskRelay(
        xthread::WakeFlag& io_wake,
        std::unique_ptr<cores::IDiskServant> servant
    ) noexcept
        : io_{.servant = std::move(servant)},
          chan_(io_wake) {}

    DiskRelay(const DiskRelay&) = delete;
    DiskRelay& operator=(const DiskRelay&) = delete;

    void seal_boot_pump() noexcept { sealed_ = true; }

    void service_rt(proto::Link& link, const proto::DiskPollDecl& poll) noexcept override;
    void order_bind(proto::DiskUnit unit, std::string_view path) noexcept override;
    [[nodiscard]] cores::MountState bind_answer(proto::DiskUnit unit) noexcept override;

    void abandon_command() noexcept override;
    void abandon_command_on(proto::DiskUnit unit) noexcept override;

    void forget() noexcept;

    void serve() noexcept override;

    [[nodiscard]] bool idle() const noexcept override { return chan_.outbound() == 0; }

    void pump_on_caller() noexcept;

    using Loan = Chan::Loan;
    void on(const DiskSlot::Ask::Bind& a, const RelayStamp& h, Loan& l, proto::Link* link) noexcept;
    void on(
        const DiskSlot::Ask::Command& a,
        const RelayStamp& h,
        Loan& l,
        proto::Link* link
    ) noexcept;
    void on(
        const DiskSlot::Ask::Resume& a,
        const RelayStamp& h,
        Loan& l,
        proto::Link* link
    ) noexcept;
    void on(
        const DiskSlot::Ask::Release& a,
        const RelayStamp& h,
        Loan& l,
        proto::Link* link
    ) noexcept;
    void misrouted(const DiskSlot::Ask& m, Loan& l, proto::Link* link) noexcept;

    void on(const DiskSlot::Ask::Bind& a, const RelayStamp& h, Job& job) noexcept;
    void on(const DiskSlot::Ask::Command& a, const RelayStamp& h, Job& job) noexcept;
    void on(const DiskSlot::Ask::Resume& a, const RelayStamp& h, Job& job) noexcept;
    void on(const DiskSlot::Ask::Release& a, const RelayStamp& h, Job& job) noexcept;
    void misrouted(const DiskSlot::Ask& m, Job& job) noexcept;

    [[nodiscard]] bool command_open() const noexcept { return rt_.command_open; }

    [[nodiscard]] std::uint32_t polls() const noexcept { return rt_.polls.get(); }

    [[nodiscard]] std::uint32_t lent() const noexcept { return rt_.lent.get(); }

    [[nodiscard]] std::uint32_t declines() const noexcept { return rt_.declines.get(); }

    [[nodiscard]] std::uint32_t acts() const noexcept { return rt_.acts.get(); }

    [[nodiscard]] std::uint32_t stale() const noexcept { return rt_.stale.get(); }

    [[nodiscard]] std::uint32_t wire_faults() const noexcept { return rt_.wire_faults.get(); }

    [[nodiscard]] std::uint32_t abandoned() const noexcept { return rt_.abandoned.get(); }

    [[nodiscard]] std::uint32_t sealed_pumps() const noexcept { return sealed_pumps_.get(); }

    [[nodiscard]] Chan::Census census() const noexcept { return chan_.census(); }

    [[nodiscard]] Chan& channel_for_test() noexcept { return chan_; }

private:
    struct Order {
        PathText path{};
        std::uint32_t seq = 0;
        std::uint32_t answered = 0;
        cores::MountState answer = cores::MountState::Pending;
        bool sent = false;
    };

    struct StaleLanding {
        DiskRelay& relay;

        void on(const DiskSlot::Ask::Command&, const RelayStamp&) noexcept {
            relay.close_command_();
        }

        void on(const DiskSlot::Ask::Resume&, const RelayStamp&) noexcept {
            relay.close_command_();
        }

        void misrouted(const DiskSlot::Ask&) noexcept {}
    };

    void land_(Chan::Loan&& l, proto::Link* link) noexcept;
    [[nodiscard]] bool write_(proto::Link& link, const proto::DiskAct& a, DiskSlot& s) noexcept;

    [[nodiscard]] bool send_orders_() noexcept;
    void poll_(proto::Link& link, const proto::DiskPollDecl& poll) noexcept;
    void act_(const RelayStamp& h, Loan& l, proto::Link* link) noexcept;
    void close_command_() noexcept;

    void begin_(const RelayStamp& h) noexcept;

    struct Rt {
        RelayStamp stamp{CoreGen{1}, ActEpoch{1}};
        bool command_open = false;
        std::optional<proto::DiskUnit> open_unit{};
        bool release_owed = false;
        infra::IndexedArray<Order, kUnits, proto::DiskUnit> orders{};
        xthread::Counter polls{}, lent{}, declines{}, acts{}, stale{}, wire_faults{}, abandoned{};
    };

    struct Io {
        std::unique_ptr<cores::IDiskServant> servant{};
        RelayStamp served{};
    };

    bool sealed_ = false;
    xthread::Counter sealed_pumps_{};
    Rt rt_{};
    Io io_{};
    Chan chan_;
};

}
