// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "app/disk_attach_seq.h"
#include "app/disk_slot.h"
#include "app/path_text.h"
#include "app/round_words.h"
#include "cores/disk_port.h"
#include "cores/disk_servant.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "infra/xthread/counter.h"
#include "infra/xthread/loan_channel.h"
#include "infra/xthread/polled.h"
#include "infra/xthread/wake_flag.h"
#include "proto/disk_poll_program.h"
#include "proto/disk_protocol_row.h"
#include "proto/link/spi_fio_queue.h"
#include "proto/types.h"

namespace mister::app {

class DiskRelay final : public cores::IDiskPort {
    TASTY_SEAT_MEDIATOR(RT, Io);

public:
    using Chan = xthread::LoanChannel<DiskSlot, kDiskDepth, SeatTag::RT, SeatTag::Io>;
    using Job = Chan::Job;

    static constexpr proto::FioWordCount kMaxRoundWords = std::max(
        proto::DiskAct::max_words(),
        proto::DiskPollProgram::kMaxWords
    );
    static_assert(kMaxRoundWords <= proto::link::tightest_round_budget_words());

    static_assert(
        proto::DiskPollProgram::kMaxWords + proto::DiskAct::max_signal_words() <= kMaxRoundWords
    );

    DiskRelay(xthread::Polled, const proto::DiskProtocolRow& row) noexcept
        : row_(row),
          chan_(xthread::Polled{}) {}

    DiskRelay(xthread::WakeFlag& io_wake, const proto::DiskProtocolRow& row) noexcept
        : row_(row),
          chan_(io_wake) {}

    DiskRelay(const DiskRelay&) = delete;
    DiskRelay& operator=(const DiskRelay&) = delete;

    [[nodiscard]] proto::DiskProtocol protocol() const noexcept { return row_.protocol; }

    void service_rt(proto::link::Link& link, const proto::DiskPollProgram& poll) noexcept override;

    [[nodiscard]] proto::FioWordCount take_round_words() noexcept;
    void order_bind(proto::DiskUnit unit, std::string_view path) noexcept override;
    [[nodiscard]] cores::MountState bind_answer(proto::DiskUnit unit) noexcept override;

    void abandon_command() noexcept override;
    void abandon_command_on(proto::DiskUnit unit) noexcept override;

    void attach(DiskAttachSeq order, infra::OptRef<cores::IDiskServant> servant) noexcept;

    [[nodiscard]] bool settle_attach(DiskAttachSeq order) noexcept;

    void serve() noexcept;

    [[nodiscard]] bool idle() const noexcept { return chan_.outbound() == 0; }

    using Loan = Chan::Loan;
    void on(
        const DiskSlot::Ask::Bind& a,
        const RelayStamp& h,
        Loan& l,
        proto::link::Link* link
    ) noexcept;
    void on(
        const DiskSlot::Ask::Command& a,
        const RelayStamp& h,
        Loan& l,
        proto::link::Link* link
    ) noexcept;
    void on(
        const DiskSlot::Ask::Resume& a,
        const RelayStamp& h,
        Loan& l,
        proto::link::Link* link
    ) noexcept;
    void on(
        const DiskSlot::Ask::Attach& a,
        const RelayStamp& h,
        Loan& l,
        proto::link::Link* link
    ) noexcept;
    void misrouted(const DiskSlot::Ask& m, Loan& l, proto::link::Link* link) noexcept;

    void on(const DiskSlot::Ask::Bind& a, const RelayStamp& h, Job& job) noexcept;
    void on(const DiskSlot::Ask::Command& a, const RelayStamp& h, Job& job) noexcept;
    void on(const DiskSlot::Ask::Resume& a, const RelayStamp& h, Job& job) noexcept;
    void on(const DiskSlot::Ask::Attach& a, const RelayStamp& h, Job& job) noexcept;
    void misrouted(const DiskSlot::Ask& m, Job& job) noexcept;

    [[nodiscard]] bool command_open() const noexcept { return rt_.command_open; }

    [[nodiscard]] std::uint32_t polls() const noexcept { return rt_.polls.get(); }

    [[nodiscard]] std::uint32_t lent() const noexcept { return rt_.lent.get(); }

    [[nodiscard]] std::uint32_t declines() const noexcept { return rt_.declines.get(); }

    [[nodiscard]] std::uint32_t acts() const noexcept { return rt_.acts.get(); }

    [[nodiscard]] std::uint32_t stale() const noexcept { return rt_.stale.get(); }

    [[nodiscard]] infra::Tally dropped() const noexcept { return infra::Tally{rt_.dropped.get()}; }

    [[nodiscard]] std::uint32_t wire_faults() const noexcept { return rt_.wire_faults.get(); }

    [[nodiscard]] std::uint32_t abandoned() const noexcept { return rt_.abandoned.get(); }

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

    struct Attachment {
        infra::OptRef<cores::IDiskServant> servant{};
        DiskAttachSeq ordered{};
        DiskAttachSeq sent{};
        DiskAttachSeq home{};
        bool owed = false;
        bool lent = false;
    };

    struct StaleLanding {
        DiskRelay& relay;

        void on(const DiskSlot::Ask::Command&, const RelayStamp& h) noexcept {
            relay.close_stale_command_(h);
        }

        void on(const DiskSlot::Ask::Resume&, const RelayStamp& h) noexcept {
            relay.close_stale_command_(h);
        }

        void on(const DiskSlot::Ask::Attach& a, const RelayStamp&) noexcept {
            relay.owe_attach_again_(a.order);
        }

        void misrouted(const DiskSlot::Ask&) noexcept {}
    };

    enum class Written : std::uint8_t { Whole, Dropped, Faulted };

    void land_(Chan::Loan&& l, proto::link::Link* link) noexcept;
    [[nodiscard]] Written write_(
        proto::link::Link& link,
        const proto::DiskAct& a,
        std::span<std::byte> bytes
    ) noexcept;

    void write_standing_(proto::link::Link& link) noexcept;

    [[nodiscard]] bool send_orders_() noexcept;

    [[nodiscard]] bool send_order_(proto::DiskUnit unit) noexcept;
    [[nodiscard]] bool send_attach_() noexcept;

    [[nodiscard]] bool poll_(proto::link::Link& link, const proto::DiskPollProgram& poll) noexcept;
    void act_(const RelayStamp& h, Loan& l, proto::link::Link* link) noexcept;
    void close_command_() noexcept;
    void close_stale_command_(const RelayStamp& h) noexcept;
    void owe_attach_again_(DiskAttachSeq order) noexcept;

    [[nodiscard]] infra::OptRef<Order> order_of_(proto::DiskUnit unit) noexcept;

    [[nodiscard]] bool serves_(const RelayStamp& h) const noexcept;

    struct Rt {
        RelayStamp stamp{CoreGen{1}, ActEpoch{1}};
        bool command_open = false;
        std::optional<proto::DiskUnit> open_unit{};

        std::optional<proto::ReplyWord> last_seen{};

        proto::DiskAct standing{};
        Attachment attach{};
        infra::IndexedArray<Order, proto::DiskUnits::kSize, proto::DiskUnit> orders{};

        RoundWords round_words{};
        xthread::Counter polls{}, lent{}, declines{}, acts{}, stale{}, dropped{}, wire_faults{},
            abandoned{};
    };

    struct Io {
        infra::OptRef<cores::IDiskServant> servant{};
        RelayStamp served{};
    };

    const proto::DiskProtocolRow& row_;
    Rt rt_{};
    Io io_{};
    Chan chan_;
};

}
