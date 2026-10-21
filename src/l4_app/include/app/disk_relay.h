// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

#include "app/disk_slot.h"
#include "app/path_text.h"
#include "cores/disk_port.h"
#include "cores/disk_servant.h"
#include "infra/counter.h"
#include "infra/loan_channel.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "proto/spi_fio_queue.h"
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

    explicit DiskRelay(xthread::Polled) noexcept : chan_(xthread::Polled{}) {}

    explicit DiskRelay(xthread::WakeFlag& io_wake) noexcept : chan_(io_wake) {}

    DiskRelay(const DiskRelay&) = delete;
    DiskRelay& operator=(const DiskRelay&) = delete;

    void install(std::unique_ptr<cores::IDiskServant> servant) noexcept;

    void seal_boot_pump() noexcept { sealed_ = true; }

    void service_rt(hal::ISpiTransport& link, const proto::DiskPollDecl& poll) noexcept override;
    void order_bind(std::uint8_t unit, std::string_view path) noexcept override;
    [[nodiscard]] cores::MountState bind_answer(std::uint8_t unit) noexcept override;

    void abandon_command() noexcept override;
    void abandon_command_on(std::uint8_t unit) noexcept override;

    void forget() noexcept;

    void serve() noexcept override;

    [[nodiscard]] bool idle() const noexcept override { return chan_.outbound() == 0; }

    void pump_on_caller() noexcept;

    using Loan = Chan::Loan;
    void on(
        const DiskSlot::Ask::Bind& a,
        const DiskSlot::Ask::Head& h,
        Loan& l,
        hal::ISpiTransport* link
    ) noexcept;
    void on(
        const DiskSlot::Ask::Command& a,
        const DiskSlot::Ask::Head& h,
        Loan& l,
        hal::ISpiTransport* link
    ) noexcept;
    void on(
        const DiskSlot::Ask::Resume& a,
        const DiskSlot::Ask::Head& h,
        Loan& l,
        hal::ISpiTransport* link
    ) noexcept;
    void on(
        const DiskSlot::Ask::Release& a,
        const DiskSlot::Ask::Head& h,
        Loan& l,
        hal::ISpiTransport* link
    ) noexcept;
    void misrouted(const DiskSlot::Ask& m, Loan& l, hal::ISpiTransport* link) noexcept;

    void on(const DiskSlot::Ask::Bind& a, const DiskSlot::Ask::Head& h, Job& job) noexcept;
    void on(const DiskSlot::Ask::Command& a, const DiskSlot::Ask::Head& h, Job& job) noexcept;
    void on(const DiskSlot::Ask::Resume& a, const DiskSlot::Ask::Head& h, Job& job) noexcept;
    void on(const DiskSlot::Ask::Release& a, const DiskSlot::Ask::Head& h, Job& job) noexcept;
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

    void land_(Chan::Loan&& l, hal::ISpiTransport* link) noexcept;
    [[nodiscard]] bool write_(
        hal::ISpiTransport& link,
        const proto::DiskAct& a,
        DiskSlot& s
    ) noexcept;

    [[nodiscard]] bool send_orders_() noexcept;
    void poll_(hal::ISpiTransport& link, const proto::DiskPollDecl& poll) noexcept;
    void act_(const DiskSlot::Ask::Head& h, Loan& l, hal::ISpiTransport* link) noexcept;
    void close_command_() noexcept;

    void begin_(const DiskSlot::Ask::Head& h) noexcept;

    struct Rt {
        std::uint16_t gen = 1;
        std::uint16_t epoch = 1;
        bool command_open = false;
        std::uint8_t open_unit = proto::kNoDiskUnit;
        bool release_owed = false;
        std::array<Order, kUnits> orders{};
        xthread::Counter polls{}, lent{}, declines{}, acts{}, stale{}, wire_faults{}, abandoned{};
    };

    struct Io {
        std::unique_ptr<cores::IDiskServant> servant{};
        std::uint16_t served_gen = 0;
    };

    bool sealed_ = false;
    xthread::Counter sealed_pumps_{};
    Rt rt_{};
    Io io_{};
    Chan chan_;
};

}
