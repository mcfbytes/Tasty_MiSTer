// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <unistd.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include "app/bitstream_programmer.h"
#include "app/board_reboot.h"
#include "app/core_builder.h"
#include "app/event.h"
#include "app/link_tx_channel.h"
#include "app/owner_push.h"
#include "app/owner_tally.h"
#include "app/park_receipt.h"
#include "app/pending_load.h"
#include "app/quiesce.h"
#include "app/readiness_watch.h"
#include "app/start_watch.h"
#include "app/types.h"
#include "app/ui_request.h"
#include "hal/thread_map.h"
#include "infra/error.h"
#include "infra/message_sum.h"
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

class CoreSwitch {
    TASTY_SEAT_RESIDENT(Main);

public:
    class Host {
    public:
        virtual void switch_minted(const UiRequest::LoadCore& req) noexcept = 0;
        virtual void drop_core_acts() = 0;
        virtual void forget_core() noexcept = 0;

        virtual void programmed(bool ok) = 0;

    protected:
        Host() = default;
        Host(const Host&) = default;
        Host& operator=(const Host&) = default;
        virtual ~Host() = default;
    };

    struct Tally {

        std::uint32_t pause_expiries = 0;

        std::uint32_t orders_superseded = 0;
        std::uint32_t loads_held = 0;
        std::uint32_t loads_cancelled = 0;

        std::uint32_t switch_belt_refusals = 0;

        std::uint32_t switches_asked = 0;
        std::uint32_t ask_refusals = 0;

        std::uint32_t load_core_refusals = 0;
        std::uint32_t stale_acks = 0;

        std::uint32_t programs_run = 0;
        std::uint32_t programs_failed = 0;
    };

    enum class Asked : std::uint8_t { Yes, Invalid, NoRoom };

    enum class Programmed : std::uint8_t { NotNeeded, Ok, Failed };

    struct StepArg {
        bool loaded = false;
    };

    static constexpr std::size_t kTeardownOps = 3;
    static_assert(proto::kLinkTxCapacity >= kTeardownOps);

    struct Wiring {
        QuiesceChannel& park;
        LinkTxChannel<SeatTag::Main>& inbox;
        const os::IClock& clock;
        BitstreamProgrammer& programmer;
        const svc::Vfs& vfs;
        CoreBuilder& builder;
        ReadinessWatch& readiness;
        const BoardReboot& reboot;
        StartWatch& start_watch;
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
          start_watch_(w.start_watch),
          owner_events_(w.events),
          owner_tally_(w.owner_tally) {}

    CoreSwitch(const CoreSwitch&) = delete;
    CoreSwitch& operator=(const CoreSwitch&) = delete;

    void grant_pause(SeatTag s, xthread::PauseLatch& latch) noexcept {
        TASTY_SEAT_BODY(CoreSwitch);
        pauses_[hal::row_index(s)] = &latch;
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

    [[nodiscard]] std::optional<std::uint32_t> try_program(Host& host);

    [[nodiscard]] bool refuse_in_switch(UiRequest::Kind kind, CorrelationTag tag) noexcept;

    void order_teardown() noexcept;
    Programmed program_at_boot(Host& host);
    [[nodiscard]] StepArg step_arg(Programmed p) const noexcept;

    [[nodiscard]] bool program_owed() const noexcept;

    [[nodiscard]] std::uint32_t gen() const noexcept { return gen_; }

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

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

    [[nodiscard]] const xthread::Telemetry<std::uint32_t, SeatTag::Main>&
    pause_expiries_cell() const noexcept {
        return pause_expiries_cell_;
    }

private:
    struct HeldLoad {
        UiRequest::LoadCore req{};
        CorrelationTag tag{};
    };

    static constexpr std::uint32_t kMaxGen = 0xFFFFu;
    static_assert(
        kMaxGen <= std::numeric_limits<std::uint16_t>::max(),
        "the gen must reach T-RT through the ApplyCore op untruncated"
    );

    static constexpr std::int64_t kPauseDeadlineNs = 1'000'000'000;

    static constexpr bool pauses_at_receipt(SeatTag s) noexcept { return s == SeatTag::Io; }

    [[nodiscard]] static proto::BindGeneration as_generation_(std::uint32_t gen) noexcept {
        return proto::BindGeneration{static_cast<std::uint16_t>(gen)};
    }

    void pause_seats_(std::uint32_t gen, bool at_receipt) noexcept;
    void resume_seats_() noexcept;
    [[nodiscard]] bool seats_paused_(std::uint32_t gen) const noexcept;

    Programmed program_(const ParkReceipt& receipt, Host& host);
    void publish_refusal_(UiRequest::Kind which, Errc code, CorrelationTag tag) noexcept;
    void advise_switch_failed_(Errc code) noexcept;
    template <class A>
    void order_(const A& alt) noexcept;
    template <class A>
    [[nodiscard]] bool order_refusable_(const A& alt) noexcept;

    QuiesceChannel& park_;
    LinkTxChannel<SeatTag::Main>& inbox_;
    const os::IClock& clock_;
    BitstreamProgrammer& programmer_;
    const svc::Vfs& vfs_;
    CoreBuilder& builder_;
    ReadinessWatch& readiness_;
    const BoardReboot& reboot_;
    StartWatch& start_watch_;
    EventQueue<SeatTag::Main>& owner_events_;
    OwnerTally& owner_tally_;
    Tally tally_{};
    StepArg pending_step_arg_{};
    std::uint32_t gen_ = 0;
    UiRequest::LoadCore pending_{};
    CorrelationTag pending_tag_{};
    bool switch_standing_ = false;
    bool switch_programmed_ = false;
    std::optional<HeldLoad> held_load_{};

    std::array<xthread::PauseLatch*, hal::kThreadSeats> pauses_{};

    std::optional<ParkReceipt> receipt_{};

    std::optional<proto::LinkOp> last_order_{};
    os::Deadline pause_due_ = os::Deadline::immediate();
    xthread::Telemetry<std::uint32_t, SeatTag::Main> pause_expiries_cell_{};
};

inline bool CoreSwitch::validate(const UiRequest::LoadCore& req) {
    TASTY_SEAT_BODY(CoreSwitch);
    return validate_load(vfs_, req);
}

inline void CoreSwitch::on_load_core(
    const UiRequest::LoadCore& req,
    CorrelationTag tag,
    Host& host
) {
    TASTY_SEAT_BODY(CoreSwitch);
    const auto refuse = [&](Errc code) {
        ++tally_.load_core_refusals;
        publish_refusal_(UiRequest::Kind::LoadCore, code, tag);
    };

    if (req.xml >= XmlKind::Mgl) {
        refuse(Errc::bad_format);
        return;
    }
    if (switch_standing_
        || readiness_.recovering()
        || reboot_.failed()
        || reboot_.owed().has_value()) {
        refuse(Errc::negotiation);
        return;
    }

    const std::optional<HeldLoad> prior = std::exchange(held_load_, std::nullopt);
    const Asked asked = ask(req, tag, host);
    if (asked == Asked::Invalid) {
        held_load_ = prior;
        return;
    }
    if (prior) {
        ++tally_.loads_cancelled;
        publish_refusal_(UiRequest::Kind::LoadCore, Errc::cancelled, prior->tag);
    }

    if (asked == Asked::NoRoom) {
        held_load_.emplace(HeldLoad{.req = req, .tag = tag});
        ++tally_.loads_held;
    }
}

inline void CoreSwitch::retry_held_load(Host& host) {
    TASTY_SEAT_BODY(CoreSwitch);
    if (!held_load_ || switch_standing_ || readiness_.recovering())
        return;
    if (proto::kLinkTxCapacity - inbox_.ring().size() < kTeardownOps || !park_.ask_room())
        return;
    const HeldLoad h = *held_load_;
    held_load_.reset();
    on_load_core(h.req, h.tag, host);
}

inline void CoreSwitch::ask_or_refuse(
    const UiRequest::LoadCore& req,
    CorrelationTag tag,
    Host& host
) {
    TASTY_SEAT_BODY(CoreSwitch);
    if (ask(req, tag, host) == Asked::NoRoom)
        publish_refusal_(UiRequest::Kind::LoadCore, Errc::would_block, tag);
}

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

    const std::uint32_t next = (gen_ >= kMaxGen) ? 1u : gen_ + 1u;
    ++tally_.switches_asked;
    if (!park_.post_ask(Quiesce{next})) {
        ++tally_.ask_refusals;
        return Asked::NoRoom;
    }
    gen_ = next;

    host.switch_minted(req);
    switch_programmed_ = false;

    if (last_order_ && !switch_standing_) {
        last_order_.reset();
        ++tally_.orders_superseded;
    }

    pause_seats_(gen_, false);
    receipt_.reset();
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
    last_order_ = infra::make<proto::LinkOp>(alt);

    if (!push_last_order())
        ++owner_tally_.order_refusals;
}

inline bool CoreSwitch::push_last_order() noexcept {
    TASTY_SEAT_BODY(CoreSwitch);
    if (!last_order_)
        return true;
    if (!push_posted(inbox_, owner_tally_, *last_order_))
        return false;
    last_order_.reset();
    switch_standing_ = false;
    resume_seats_();
    return true;
}

inline void CoreSwitch::pause_seats_(std::uint32_t gen, bool at_receipt) noexcept {
    for (std::size_t i = 0; i < pauses_.size(); ++i) {
        if (pauses_[i] == nullptr)
            continue;
        if (pauses_at_receipt(static_cast<SeatTag>(i + 1)) == at_receipt)
            pauses_[i]->pause(gen);
    }
}

inline void CoreSwitch::resume_seats_() noexcept {
    for (xthread::PauseLatch* l : pauses_)
        if (l != nullptr)
            l->resume();
}

inline bool CoreSwitch::seats_paused_(std::uint32_t gen) const noexcept {
    for (const xthread::PauseLatch* l : pauses_)
        if (l != nullptr && !l->paused_at(gen))
            return false;
    return true;
}

inline bool CoreSwitch::program_owed() const noexcept {
    TASTY_SEAT_BODY(CoreSwitch);
    return receipt_.has_value() && !readiness_.recovering() && seats_paused_(gen_);
}

inline unsigned CoreSwitch::take_park_acks() {
    TASTY_SEAT_BODY(CoreSwitch);
    unsigned taken = 0;
    while (const auto receipt = park_.take_ack()) {
        ++taken;
        if (receipt->gen() != gen_) {
            ++tally_.stale_acks;
            continue;
        }

        receipt_.emplace(*receipt);

        pause_seats_(gen_, true);
        pause_due_ = os::Deadline::in(clock_, std::chrono::nanoseconds{kPauseDeadlineNs});
    }
    return taken;
}

inline std::optional<std::uint32_t> CoreSwitch::try_program(Host& host) {
    TASTY_SEAT_BODY(CoreSwitch);
    if (!receipt_ || readiness_.recovering())
        return std::nullopt;
    const bool all = seats_paused_(gen_);
    if (!all && !pause_due_.expired(clock_))
        return std::nullopt;

    if (!all)
        pause_expiries_cell_.publish(++tally_.pause_expiries);
    const ParkReceipt r = *receipt_;
    receipt_.reset();
    (void)::unlink("/tmp/GAMEID");

    (void)::unlink("/tmp/CONFSTR");

    builder_.drop_windows(r);
    const Programmed p = program_(r, host);
    pending_step_arg_ = step_arg(p);

    if (p == Programmed::Ok) {
        switch_programmed_ = true;
        readiness_.clear_owed();
    }

    start_watch_.arm_bound();
    if (p == Programmed::Failed) {
        start_watch_.no_identity_wait();
        end(proto::LinkOp::ApplyCore{
            .loaded = pending_step_arg_.loaded,
            .gen = as_generation_(gen_)
        });
        return std::nullopt;
    }
    return gen_;
}

inline CoreSwitch::Programmed CoreSwitch::program_(const ParkReceipt& receipt, Host& host) {

    if (pending_.path.empty())
        return Programmed::NotNeeded;

    auto rel = resolve_bitstream(vfs_, pending_.path.view(), pending_.xml);
    if (!rel) {
        ++tally_.programs_failed;
        return Programmed::Failed;
    }
    ++tally_.programs_run;
    const bool ok = programmer_.program(receipt, *rel).has_value();
    if (!ok)
        ++tally_.programs_failed;
    host.programmed(ok);
    return ok ? Programmed::Ok : Programmed::Failed;
}

inline CoreSwitch::Programmed CoreSwitch::program_at_boot(Host& host) {
    TASTY_SEAT_BODY(CoreSwitch);
    if (pending_.path.empty())
        return Programmed::NotNeeded;
    auto rel = resolve_bitstream(vfs_, pending_.path.view(), pending_.xml);
    if (!rel) {
        ++tally_.programs_failed;
        return Programmed::Failed;
    }
    ++tally_.programs_run;
    const bool ok = programmer_.program_at_boot(*rel).has_value();
    if (!ok)
        ++tally_.programs_failed;
    host.programmed(ok);
    return ok ? Programmed::Ok : Programmed::Failed;
}

inline CoreSwitch::StepArg CoreSwitch::step_arg(Programmed p) const noexcept {
    TASTY_SEAT_BODY(CoreSwitch);

    if (p == Programmed::NotNeeded)
        return StepArg{.loaded = true};
    return StepArg{.loaded = p == Programmed::Ok};
}

inline bool CoreSwitch::refuse_in_switch(UiRequest::Kind kind, CorrelationTag tag) noexcept {
    TASTY_SEAT_BODY(CoreSwitch);
    if (reboot_.owed().has_value()) {
        ++owner_tally_.reboot_refusals;
        publish_refusal_(kind, Errc::negotiation, tag);
        return true;
    }
    if (!switch_standing_)
        return false;
    ++tally_.switch_belt_refusals;
    publish_refusal_(kind, Errc::negotiation, tag);
    return true;
}

inline void CoreSwitch::order_teardown() noexcept {
    TASTY_SEAT_BODY(CoreSwitch);
    order_(proto::LinkOp::DropCore{});
    (void)order_refusable_(
        proto::LinkOp::BindDecoders{
            .table = proto::LinkOp::DecoderTable::Census,
            .gen = as_generation_(gen_)
        }
    );
    order_(proto::LinkOp::HoldReset{});
}

template <class A>
inline void CoreSwitch::order_(const A& alt) noexcept {
    (void)push_dropping(inbox_, owner_tally_, alt);
}

template <class A>
inline bool CoreSwitch::order_refusable_(const A& alt) noexcept {
    return push_refusing(inbox_, owner_tally_, alt);
}

inline void CoreSwitch::publish_refusal_(
    UiRequest::Kind which,
    Errc code,
    CorrelationTag tag
) noexcept {
    (void)owner_events_.push(
        infra::make<Event>(
            Event::RequestRefused{.which = which, .why = code},
            Event::Head{EmitSite{ERR_SITE()}, {}, tag}
        )
    );
}

inline void CoreSwitch::advise_switch_failed_(Errc code) noexcept {

    (void)owner_events_.push(
        infra::make<Event>(
            Event::SessionAdvisory{.why = code},
            Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}
        )
    );
}

}
