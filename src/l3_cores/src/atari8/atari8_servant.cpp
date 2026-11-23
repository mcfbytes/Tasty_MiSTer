// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/atari8_servant.h"

#include <utility>

#include "cores/atari8/atari8_decl.h"
#include "cores/atari8/sio_types.h"
#include "infra/message_sum.h"
#include "os/raw_monotonic_clock.h"

namespace mister::cores::atari8 {

proto::MailboxAct Atari8Servant::serve(
    proto::MailboxSelectIndex select,
    const proto::user_io::PollFrame& frame,
    ActBytes bytes,
    BulkBytes
) noexcept {
    TASTY_SEAT_BODY(Atari8Servant);
    if (frame.kept != proto::ReplyCount{}) {
        if (select == kStatusSelect) {
            status_ = Atari8Status{frame.replies[kStatusReply]};
            sequencer_.on_status(status_);
        } else if (select == kRxSelect && disks_) {
            disks_->on_rx(SioRxStatus{frame.replies[kRxStatusReply]});
        }
    }
    if (in_flight_)
        return {};
    return compose_(bytes);
}

proto::MailboxAct Atari8Servant::settle(
    const proto::MailboxResult& result,
    ActBytes bytes,
    BulkBytes
) noexcept {
    TASTY_SEAT_BODY(Atari8Servant);
    const std::optional<Part> part = in_flight_;
    in_flight_.reset();
    const os::RawMonotonicClock::time_point now = os::RawMonotonicClock::now();
    if (part) {
        switch (*part) {
            case Part::Sequencer:
                sequencer_.settled(result);
                break;
            case Part::Disks:
                if (disks_ && disks_->settled(result, bytes, status_))
                    sequencer_.release_hold();
                break;
            case Part::Carts:
                carts_.settled(result, bytes, now);
                break;
            case Part::Tape:
                tape_.settled(result, bytes, now);
                break;
        }
    }
    return compose_(bytes);
}

void Atari8Servant::begin_core() noexcept {
    TASTY_SEAT_BODY(Atari8Servant);
    in_flight_.reset();
    carts_turn_ = false;
    sequencer_.begin_core();
    if (disks_)
        disks_->begin_core();
    carts_.begin_core();
    tape_.begin_core();
    status_ = {};
}

void Atari8Servant::rebind(std::string_view stem, bool present) noexcept {
    TASTY_SEAT_BODY(Atari8Servant);
    (void)stem;
    (void)present;
    in_flight_.reset();
    carts_.begin_core();
    tape_.unmount();
    sequencer_.order_reset();
    if (disks_)
        disks_->reset();
}

void Atari8Servant::mount(SioSlot slot, std::string_view path) {
    TASTY_SEAT_BODY(Atari8Servant);
    if (disks_)
        disks_->mount(slot, path, status_);
}

void Atari8Servant::hold_cart(CartSlot slot, CartWriteBack::Held held) noexcept {
    TASTY_SEAT_BODY(Atari8Servant);
    carts_.hold(slot, std::move(held));
}

void Atari8Servant::drop_cart(CartSlot slot) noexcept {
    TASTY_SEAT_BODY(Atari8Servant);
    carts_.drop(slot);
}

void Atari8Servant::mount_tape(std::unique_ptr<svc::IFile> file, bool boot) noexcept {
    TASTY_SEAT_BODY(Atari8Servant);
    if (boot) {
        carts_.drop(CartSlot::First);
        carts_.drop(CartSlot::Second);
        sequencer_.order_reset();
    }
    tape_.mount(std::move(file), boot);
}

namespace {

[[nodiscard]] proto::MailboxAct::NotBefore due_at(const proto::MailboxAct& act) noexcept {
    if (const auto r = infra::as<proto::MailboxAct::CommandRun>(act))
        return r->not_before;
    if (const auto d = infra::as<proto::MailboxAct::AddressedDownload>(act))
        return d->not_before;
    if (const auto u = infra::as<proto::MailboxAct::AddressedUpload>(act))
        return u->not_before;
    return {};
}

[[nodiscard]] bool live(const proto::MailboxAct& act) noexcept {
    return infra::kind_of(act) != proto::MailboxAct::Kind::None;
}

}

proto::MailboxAct Atari8Servant::sent_(Part part, const proto::MailboxAct& act) noexcept {
    if (live(act))
        in_flight_ = part;
    return act;
}

proto::MailboxAct Atari8Servant::compose_(ActBytes bytes) noexcept {
    const os::RawMonotonicClock::time_point now = os::RawMonotonicClock::now();
    if (sequencer_.take_warm_boot() && disks_)
        disks_->warm_booted();
    sequencer_.on_carts(carts_.selects());
    const proto::MailboxAct reset = sent_(Part::Sequencer, sequencer_.next(bytes));
    if (in_flight_)
        return reset;
    if (!disks_)
        return carts_or_tape_(bytes, now);
    if (const std::optional<Atari8Disks::ColdBoot> boot = disks_->take_cold_boot()) {
        sequencer_.order_cold_boot(*boot == Atari8Disks::ColdBoot::Held);
        return sent_(Part::Sequencer, sequencer_.next(bytes));
    }
    if (carts_turn_) {
        const proto::MailboxAct other = carts_or_tape_(bytes, now);
        if (in_flight_ && due_at(other) <= now) {
            carts_turn_ = false;
            return other;
        }
        in_flight_.reset();
    }
    const proto::MailboxAct disk = sent_(Part::Disks, disks_->next(bytes, status_));
    if (in_flight_) {
        carts_turn_ = true;
        return disk;
    }
    return carts_or_tape_(bytes, now);
}

proto::MailboxAct Atari8Servant::carts_or_tape_(
    ActBytes bytes,
    proto::MailboxAct::NotBefore now
) noexcept {
    const proto::MailboxAct carts = carts_.next(bytes, now);
    if (live(carts) && due_at(carts) <= now)
        return sent_(Part::Carts, carts);
    const proto::MailboxAct tape = tape_.next(bytes, now);
    if (live(tape) && (!live(carts) || due_at(tape) < due_at(carts)))
        return sent_(Part::Tape, tape);
    return live(carts) ? sent_(Part::Carts, carts_.next(bytes, now)) : carts;
}

}
