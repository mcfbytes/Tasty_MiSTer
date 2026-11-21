// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "app/round_words.h"
#include "app/save_upload_slot.h"
#include "cores/nvram_save.h"
#include "cores/save_servant.h"
#include "hal/spi_transport.h"
#include "infra/xthread/counter.h"
#include "infra/xthread/loan_channel.h"
#include "infra/xthread/polled.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/wake_flag.h"
#include "proto/fio_tenant.h"
#include "proto/save_act.h"
#include "proto/link/spi_fio_queue.h"
#include "proto/types.h"
#include "proto/link/upload_session.h"
#include "svc/io_coworker.h"

namespace mister::app {

class SaveUploadRelay final : public proto::IFioTenant, public svc::IIoCoworker {
    TASTY_SEAT_MEDIATOR(RT, Io);

public:
    using Chan = xthread::LoanChannel<SaveUploadSlot, kSaveUploadDepth, SeatTag::RT, SeatTag::Io>;
    using Job = Chan::Job;
    using Loan = Chan::Loan;
    using Ask = SaveUploadSlot::Ask;

    static constexpr proto::FioWordCount kMaxRoundWords{1u + proto::kSavePieceWords};

    static constexpr proto::FioWordCount kCutWords{2};

    static constexpr std::uint32_t kActLivenessRounds = 64;

    explicit SaveUploadRelay(
        hal::ISpiTransport& link,
        xthread::Polled,
        std::unique_ptr<cores::ISaveServant> servant
    ) noexcept
        : link_(link),
          io_{.servant = std::move(servant)},
          chan_(xthread::Polled{}) {}

    explicit SaveUploadRelay(
        hal::ISpiTransport& link,
        xthread::WakeFlag& io_wake,
        std::unique_ptr<cores::ISaveServant> servant
    ) noexcept
        : link_(link),
          io_{.servant = std::move(servant)},
          chan_(io_wake) {}

    SaveUploadRelay(const SaveUploadRelay&) = delete;
    SaveUploadRelay& operator=(const SaveUploadRelay&) = delete;

    [[nodiscard]] bool order(const cores::mra::NvramSave& image, std::string_view rel) noexcept;

    void service_rt(proto::FioWordCount budget_words, bool fio_free) noexcept;

    void forget() noexcept;

    [[nodiscard]] bool settled() const noexcept;

    [[nodiscard]] bool drained() const noexcept;

    [[nodiscard]] proto::FioWordCount take_round_words() noexcept;

    [[nodiscard]] bool holds_fio() const noexcept override { return rt_.open.has_value(); }

    void yield_to_reset() noexcept override;

    static constexpr svc::LaneClass kLaneClass = svc::LaneClass::Bulk;

    void serve() noexcept override;

    [[nodiscard]] bool idle() const noexcept override { return chan_.outbound() == 0; }

    void on(const Ask::Start& a, const RelayStamp& h, Job& job) noexcept;
    void on(const Ask::Resume& a, const RelayStamp& h, Job& job) noexcept;
    void misrouted(const Ask& m, Job& job) noexcept;

    [[nodiscard]] std::uint32_t started() const noexcept { return rt_.started.get(); }

    [[nodiscard]] std::uint32_t acts() const noexcept { return rt_.acts.get(); }

    [[nodiscard]] std::uint32_t saved() const noexcept { return rt_.saved.get(); }

    [[nodiscard]] std::uint32_t failed() const noexcept { return rt_.failed.get(); }

    [[nodiscard]] std::uint32_t refused() const noexcept { return rt_.refused.get(); }

    [[nodiscard]] std::uint32_t stale() const noexcept { return rt_.stale.get(); }

    [[nodiscard]] std::uint32_t wire_faults() const noexcept { return rt_.wire_faults.get(); }

    [[nodiscard]] std::uint32_t liveness_drops() const noexcept { return rt_.liveness_drops.get(); }

    [[nodiscard]] Chan::Census census() const noexcept { return chan_.census(); }

private:
    struct Order {
        cores::mra::NvramSave image{};
        FixedStr<kConfigRelCap, StrFit::Reject> rel{};
        bool owed = false;
    };

    void land_(Loan&& l) noexcept;

    [[nodiscard]] bool write_(const proto::SaveAct& a, Loan& l) noexcept;
    void start_() noexcept;

    void cut_() noexcept;
    void end_pull_() noexcept;

    [[nodiscard]] RelayStamp head_() const noexcept { return rt_.stamp; }

    struct Rt {
        RelayStamp stamp{CoreGen{1}, ActEpoch{1}};
        bool pull_out = false;
        hal::Width width = hal::Width::Byte;
        std::optional<proto::link::UploadSession> open{};
        Order order{};
        RoundWords round_words{};
        std::uint32_t unanswered = 0;
        xthread::Counter started{}, acts{}, saved{}, failed{}, refused{}, stale{}, wire_faults{},
            liveness_drops{};
    };

    struct Io {
        std::unique_ptr<cores::ISaveServant> servant{};
        RelayStamp serving{};
    };

    hal::ISpiTransport& link_;
    Rt rt_{};
    Io io_{};
    Chan chan_;
};

}
