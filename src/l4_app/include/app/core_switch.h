// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "app/all_leases_home.h"
#include "app/bitstream_programmer.h"
#include "app/board_reboot.h"
#include "app/core_builder.h"
#include "app/event.h"
#include "app/link_tx_channel.h"
#include "app/owner_push.h"
#include "app/owner_tally.h"
#include "app/park_receipt.h"
#include "app/bitstream_resolve.h"
#include "app/switch_park.h"
#include "app/readiness_watch.h"
#include "app/types.h"
#include "app/ui_request.h"
#include "hal/thread_map.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "infra/pause_gen.h"
#include "infra/counter.h"
#include "infra/pause_latch.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "os/clock.h"
#include "os/deadline.h"
#include "proto/link_op.h"
#include "proto/types.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

[[nodiscard]] constexpr xthread::PauseGen pause_gen_of(proto::BindGeneration gen) noexcept {
    return xthread::PauseGen{gen.v};
}

class CoreSwitch {
    TASTY_SEAT_RESIDENT(Main);

public:
    class Host {
    public:
        virtual void switch_minted(const UiRequest::LoadCore& req) noexcept = 0;
        virtual void drop_core_acts() = 0;
        virtual void forget_core() noexcept = 0;

        virtual void programmed(bool ok) = 0;

        virtual void pause_wedged() noexcept = 0;

    protected:
        Host() = default;
        Host(const Host&) = default;
        Host& operator=(const Host&) = default;
        virtual ~Host() = default;
    };

    struct Tally {

        xthread::Counter pause_expiries{};

        std::uint32_t orders_superseded = 0;
        std::uint32_t loads_held = 0;
        std::uint32_t loads_cancelled = 0;

        std::uint32_t switch_belt_refusals = 0;

        std::uint32_t switches_asked = 0;

        std::uint32_t load_core_refusals = 0;

        std::uint32_t programs_run = 0;
        std::uint32_t programs_failed = 0;

        Error last_program_error{};
    };

    enum class Asked : std::uint8_t { Yes, Invalid, NoRoom };

    enum class Programmed : std::uint8_t { NotNeeded, Ok, Failed };

    struct StepArg {
        bool loaded = false;
    };

    static constexpr std::size_t kTeardownOps = 3;
    static_assert(proto::kLinkTxCapacity >= kTeardownOps);

    struct Wiring {
        SwitchPark& park;
        LinkTxChannel<SeatTag::Main>& inbox;
        const os::IClock& clock;
        BitstreamProgrammer& programmer;
        const svc::Vfs& vfs;
        CoreBuilder& builder;
        ReadinessWatch& readiness;
        const BoardReboot& reboot;
        EventQueue<SeatTag::Main>& events;
        OwnerTally& owner_tally;
    };

    explicit CoreSwitch(const Wiring& w) noexcept
        : park_(w.park),
          inbox_(w.inbox),
          clock_(w.clock),
          programmer_(w.programmer),
          vfs_(w.vfs),
          builder_(w.builder),
          readiness_(w.readiness),
          reboot_(w.reboot),
          owner_events_(w.events.producer()),
          owner_tally_(w.owner_tally) {}

    CoreSwitch(const CoreSwitch&) = delete;
    CoreSwitch& operator=(const CoreSwitch&) = delete;

    void grant_pause(const hal::ThreadRole& row, xthread::PauseLatch& latch) noexcept {
        TASTY_SEAT_BODY(CoreSwitch);
        seats_[hal::row_index(row.seat)] = GrantedSeat{
            row.seat,
            infra::OptRef<xthread::PauseLatch>{latch},
            row.ddr,
            row.pause_at
        };
    }

    void on_load_core(const UiRequest::LoadCore& req, CorrelationTag tag, Host& host);

    void retry_held_load(Host& host);
    Asked ask(const UiRequest::LoadCore& req, CorrelationTag tag, Host& host);

    [[nodiscard]] bool validate(const UiRequest::LoadCore& req);

    void ask_or_refuse(const UiRequest::LoadCore& req, CorrelationTag tag, Host& host);

    template <class A>
    void end(A alt) noexcept;

    bool push_last_order() noexcept;

    unsigned take_park_acks();

    [[nodiscard]] bool receipt_owed() const noexcept;

    [[nodiscard]] std::optional<proto::BindGeneration> try_program(Host& host);

    [[nodiscard]] bool refuse_in_switch(UiRequest::Kind kind, CorrelationTag tag) noexcept;

    void order_teardown() noexcept;
    Programmed program_at_boot(Host& host);
    [[nodiscard]] StepArg step_arg(Programmed p) const noexcept;

    [[nodiscard]] bool program_owed() const noexcept;

    [[nodiscard]] proto::BindGeneration bind_generation() const noexcept { return gen_; }

    [[nodiscard]] const UiRequest::LoadCore& pending() const noexcept { return pending_; }

    [[nodiscard]] CorrelationTag pending_tag() const noexcept { return pending_tag_; }

    void latch_pending(const UiRequest::LoadCore& req, CorrelationTag tag) noexcept {
        TASTY_SEAT_BODY(CoreSwitch);
        pending_ = req;
        pending_tag_ = tag;
    }

    [[nodiscard]] StepArg pending_step_arg() const noexcept { return pending_step_arg_; }

    void latch_step_arg(StepArg arg) noexcept {
        TASTY_SEAT_BODY(CoreSwitch);
        pending_step_arg_ = arg;
    }

    [[nodiscard]] bool switch_standing() const noexcept { return switch_standing_; }

    [[nodiscard]] bool switch_programmed() const noexcept { return switch_programmed_; }

    [[nodiscard]] bool last_order_owed() const noexcept { return last_order_.has_value(); }

    [[nodiscard]] bool load_held() const noexcept { return held_load_.has_value(); }

    [[nodiscard]] bool receipt_held() const noexcept { return receipt_.has_value(); }

    [[nodiscard]] const os::Deadline& pause_due() const noexcept { return pause_due_; }

    [[nodiscard]] bool drop_receipt() noexcept {
        TASTY_SEAT_BODY(CoreSwitch);
        return std::exchange(receipt_, std::nullopt).has_value();
    }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

    [[nodiscard]] bool lease_holding() const noexcept { return lease_hold_.has_value(); }

    [[nodiscard]] const xthread::Counter& pause_expiries_cell() const noexcept {
        return tally_.pause_expiries;
    }

private:
    struct HeldLoad {
        UiRequest::LoadCore req{};
        CorrelationTag tag{};
    };

    static constexpr std::int64_t kPauseDeadlineNs = 1'000'000'000;

    static constexpr std::int64_t kLeaseHoldWarnNs = 1'000'000'000;

    struct LeaseHold {
        os::Deadline since;
        bool warned = false;
    };

    void pause_seats_(proto::BindGeneration gen, hal::PauseEdge edge) noexcept;
    [[nodiscard]] bool hold_on_lease_() noexcept;
    void resume_seats_() noexcept;
    [[nodiscard]] bool seats_paused_(proto::BindGeneration gen) const noexcept;
    [[nodiscard]] bool mapping_owner_late_(proto::BindGeneration gen) const noexcept;

    Programmed program_(const ParkReceipt& receipt, AllLeasesHome home, Host& host);
    void publish_refusal_(UiRequest::Kind which, Errc code, CorrelationTag tag) noexcept;
    void advise_switch_failed_(Errc code) noexcept;

    void advise_switch_held_() noexcept;
    template <class A>
    void order_(const A& alt) noexcept;
    template <class A>
    [[nodiscard]] bool order_refusable_(const A& alt) noexcept;

    SwitchPark& park_;
    LinkTxChannel<SeatTag::Main>& inbox_;
    const os::IClock& clock_;
    BitstreamProgrammer& programmer_;
    const svc::Vfs& vfs_;
    CoreBuilder& builder_;
    ReadinessWatch& readiness_;
    const BoardReboot& reboot_;
    EventQueue<SeatTag::Main>::Producer owner_events_;
    OwnerTally& owner_tally_;
    Tally tally_{};
    StepArg pending_step_arg_{};
    proto::BindGeneration gen_{};
    UiRequest::LoadCore pending_{};
    CorrelationTag pending_tag_{};
    bool switch_standing_ = false;
    bool switch_programmed_ = false;
    std::optional<HeldLoad> held_load_{};

    struct GrantedSeat {
        SeatTag seat = SeatTag::Unbound;
        infra::OptRef<xthread::PauseLatch> latch{};
        hal::DdrMapping ddr = hal::DdrMapping::None;
        hal::PauseEdge pause_at = hal::PauseEdge::Ask;

        [[nodiscard]] bool granted() const noexcept { return latch.has_value(); }
    };

    std::array<GrantedSeat, hal::kThreadSeats> seats_{};

    std::optional<ParkReceipt> receipt_{};
    proto::BindGeneration minted_{};

    std::optional<proto::LinkOp> last_order_{};
    os::Deadline pause_due_ = os::Deadline::immediate();
    std::optional<LeaseHold> lease_hold_{};
};

inline CoreSwitch::Asked CoreSwitch::ask(
    const UiRequest::LoadCore& req,
    CorrelationTag tag,
    Host& host
) {
    TASTY_SEAT_BODY(CoreSwitch);

    if (!validate(req)) {

        advise_switch_failed_(Errc::not_found);
        return Asked::Invalid;
    }

    if (req.path.empty() && readiness_.fabric_unconfigured()) {
        advise_switch_failed_(Errc::fpga_not_ready);
        return Asked::Invalid;
    }

    if (proto::kLinkTxCapacity - inbox_.ring().size() < kTeardownOps) {
        ++owner_tally_.order_refusals;
        return Asked::NoRoom;
    }

    const proto::BindGeneration next = gen_.next();
    ++tally_.switches_asked;
    park_.ask(pause_gen_of(next));
    gen_ = next;

    host.switch_minted(req);
    switch_programmed_ = false;

    if (last_order_ && !switch_standing_) {
        last_order_.reset();
        ++tally_.orders_superseded;
    }

    pause_seats_(gen_, hal::PauseEdge::Ask);
    receipt_.reset();
    minted_ = proto::BindGeneration{};
    lease_hold_.reset();
    host.drop_core_acts();
    pending_ = req;
    pending_tag_ = tag;

    host.forget_core();
    order_teardown();
    switch_standing_ = true;
    return Asked::Yes;
}

template <class A>
inline void CoreSwitch::end(A alt) noexcept {
    TASTY_SEAT_BODY(CoreSwitch);
    alt.tag = pending_tag_;
    lease_hold_.reset();
    last_order_ = infra::make<proto::LinkOp>(alt);

    if (!push_last_order())
        ++owner_tally_.order_refusals;
}

template <class A>
inline void CoreSwitch::order_(const A& alt) noexcept {
    (void)push_dropping(inbox_, owner_tally_, alt);
}

template <class A>
inline bool CoreSwitch::order_refusable_(const A& alt) noexcept {
    return push_refusing(inbox_, owner_tally_, alt);
}

}
