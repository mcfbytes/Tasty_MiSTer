// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/atari8_sequencer.h"

#include "infra/message_sum.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"

namespace mister::cores::atari8 {

using wire::Level;
using wire::Pause;
using wire::ReadRegister;
using wire::WriteRegister;

namespace {

void add_(proto::MailboxRun& run, const proto::MailboxRunCommand& command) noexcept {
    (void)run.add(command);
}
}

void Atari8Sequencer::begin_core() noexcept {
    TASTY_SEAT_BODY(Atari8Sequencer);
    phase_ = RebootPhase::Rest;
    status_.reset();
    pause_.reset();
    soft_held_ = false;
    cold_held_ = false;
    hold_ = false;
    warm_began_ = false;
}

void Atari8Sequencer::order_reset() noexcept {
    TASTY_SEAT_BODY(Atari8Sequencer);
    phase_ = RebootPhase::ResetHead;
    pause_.reset();
    hold_ = false;
}

void Atari8Sequencer::order_cold_boot(bool hold) noexcept {
    TASTY_SEAT_BODY(Atari8Sequencer);
    hold_ = hold;
    begin_(Boot::Cold, Half::Whole);
}

void Atari8Sequencer::release_hold() noexcept {
    TASTY_SEAT_BODY(Atari8Sequencer);
    hold_ = false;
    pause_ = Pause::Run;
}

bool Atari8Sequencer::held() const noexcept {
    TASTY_SEAT_BODY(Atari8Sequencer);
    return hold_;
}

bool Atari8Sequencer::take_warm_boot() noexcept {
    TASTY_SEAT_BODY(Atari8Sequencer);
    const bool began = warm_began_;
    warm_began_ = false;
    return began;
}

void Atari8Sequencer::on_status(Atari8Status status) noexcept {
    TASTY_SEAT_BODY(Atari8Sequencer);
    status_ = status;
}

void Atari8Sequencer::on_carts(CartSelects carts) noexcept {
    TASTY_SEAT_BODY(Atari8Sequencer);
    carts_ = carts;
}

proto::MailboxAct Atari8Sequencer::next(ActBytes bytes) noexcept {
    TASTY_SEAT_BODY(Atari8Sequencer);
    if (phase_ == RebootPhase::Rest)
        read_edges_();
    switch (phase_) {
        case RebootPhase::Rest:
            return {};
        case RebootPhase::Mirror: {
            proto::MailboxRun run{bytes};
            add_pause_(run, status_ ? status_->pause_mirror() : Pause::Run);
            return wire::run_act(run);
        }
        case RebootPhase::ResetHead:
            return reset_head_(bytes);
        case RebootPhase::Head:
            return head_(bytes);
        case RebootPhase::ResetClear:
        case RebootPhase::Fill:
            return block_(bytes);
        case RebootPhase::Probe:
            return probe_(bytes);
        case RebootPhase::Line:
            return line_(bytes);
    }
    return {};
}

void Atari8Sequencer::settled(const proto::MailboxResult& result) noexcept {
    TASTY_SEAT_BODY(Atari8Sequencer);
    if (result.end != proto::MailboxResult::End::Done) {

        pause_.reset();
        return;
    }
    advance_(result);
}

void Atari8Sequencer::read_edges_() noexcept {
    if (!status_ || hold_)
        return;
    const Atari8Status s = *status_;
    const bool held = soft_held_ || cold_held_;
    if (!held && pause_ != s.pause_mirror()) {
        phase_ = RebootPhase::Mirror;
        return;
    }
    const bool soft = machine_ == Atari8Machine::Computer && s.soft_boot();
    if (soft != soft_held_) {
        soft_held_ = soft;
        begin_(Boot::Warm, soft ? Half::Assert : Half::Release);
    } else if (s.cold_boot() != cold_held_) {
        cold_held_ = s.cold_boot();
        begin_(Boot::Cold, cold_held_ ? Half::Assert : Half::Release);
    }
}

void Atari8Sequencer::begin_(Boot boot, Half half) noexcept {
    boot_ = boot;
    half_ = half;
    line_reg_ = WriteRegister::Reset;
    if (asserts_() && boot_ == Boot::Warm)
        warm_began_ = true;
    if (asserts_() && boot_ == Boot::Cold) {
        phase_ = RebootPhase::Head;
        return;
    }
    switch (machine_) {
        case Atari8Machine::Computer:
            phase_ = RebootPhase::Probe;
            return;
        case Atari8Machine::Console:
            phase_ = RebootPhase::Line;
            return;
    }
}

void Atari8Sequencer::advance_(const proto::MailboxResult& result) noexcept {
    switch (phase_) {
        case RebootPhase::Rest:
            return;
        case RebootPhase::Mirror:
        case RebootPhase::Line:
            phase_ = RebootPhase::Rest;
            return;
        case RebootPhase::ResetHead:
            switch (machine_) {
                case Atari8Machine::Computer:
                    begin_(Boot::Cold, Half::Whole);
                    return;
                case Atari8Machine::Console:
                    fill_ = Atari8Fill::kConsoleCart;
                    phase_ = RebootPhase::ResetClear;
                    return;
            }
            return;
        case RebootPhase::ResetClear:
            fill_ = fill_.after_block();
            if (fill_.done())
                begin_(Boot::Cold, Half::Whole);
            return;
        case RebootPhase::Head:
            switch (machine_) {
                case Atari8Machine::Computer:
                    fill_ = Atari8Fill::kComputerRam;
                    break;
                case Atari8Machine::Console:
                    fill_ = Atari8Fill::kConsoleRam;
                    break;
            }
            phase_ = RebootPhase::Fill;
            return;
        case RebootPhase::Fill:
            fill_ = fill_.after_block();
            if (!fill_.done())
                return;
            switch (machine_) {
                case Atari8Machine::Computer:
                    phase_ = RebootPhase::Probe;
                    return;
                case Atari8Machine::Console:
                    phase_ = RebootPhase::Line;
                    return;
            }
            return;
        case RebootPhase::Probe: {

            const proto::user_io::PollFrame& replies = result.replies;
            if (replies.kept == proto::ReplyCount{})
                return;
            const Atari8Status probed{replies.replies.prefix(replies.kept).back()};

            const bool rnmi = probed.mode_800() && (boot_ == Boot::Warm || hold_);
            line_reg_ = rnmi ? WriteRegister::ResetRnmi : WriteRegister::Reset;
            phase_ = RebootPhase::Line;
            return;
        }
    }
}

void Atari8Sequencer::add_pause_(proto::MailboxRun& run, Pause level) noexcept {
    add_(run, wire::pause(level));
    pause_ = level;
}

proto::MailboxAct Atari8Sequencer::reset_head_(ActBytes bytes) noexcept {
    proto::MailboxRun run{bytes};
    add_pause_(run, Pause::Hold);
    switch (machine_) {
        case Atari8Machine::Computer:
            add_(run, wire::set(WriteRegister::TapeReset, Level::High));
            add_(run, wire::set(WriteRegister::TapeReset, Level::Low));
            break;
        case Atari8Machine::Console:
            add_(run, wire::set(WriteRegister::Cart1Select, Level::Low));
            break;
    }
    return wire::run_act(run);
}

proto::MailboxAct Atari8Sequencer::head_(ActBytes bytes) noexcept {
    proto::MailboxRun run{bytes};
    add_pause_(run, Pause::Hold);
    switch (machine_) {
        case Atari8Machine::Computer:
            add_(run, wire::set(WriteRegister::Freezer, Level::Low));
            add_(run, wire::set(WriteRegister::Cart1Select, Level::Low));
            add_(run, wire::set(WriteRegister::Cart2Select, Level::Low));
            break;
        case Atari8Machine::Console:
            break;
    }
    return wire::run_act(run);
}

proto::MailboxAct Atari8Sequencer::probe_(ActBytes bytes) noexcept {
    proto::MailboxRun run{bytes};
    if (asserts_()) {
        if (boot_ == Boot::Warm)
            add_pause_(run, Pause::Hold);
        if (boot_ == Boot::Cold && !carts_.first.off())
            add_(run, wire::set_cart(WriteRegister::Cart1Select, carts_.first));
        if (boot_ == Boot::Cold && !carts_.second.off())
            add_(run, wire::set_cart(WriteRegister::Cart2Select, carts_.second));
        add_(run, wire::set(WriteRegister::XexLoader, Level::Low));
    }
    add_(run, wire::get(ReadRegister::Status1));
    return wire::run_act(run);
}

proto::MailboxAct Atari8Sequencer::line_(ActBytes bytes) noexcept {
    proto::MailboxRun run{bytes};
    if (asserts_())
        add_(run, wire::set(line_reg_, Level::High));
    if (releases_())
        add_(run, wire::set(line_reg_, Level::Low));
    switch (machine_) {
        case Atari8Machine::Computer:
            add_(run, wire::get(ReadRegister::Status3));
            if (releases_() && boot_ == Boot::Cold)
                add_(run, wire::set(WriteRegister::Freezer, Level::High));
            break;
        case Atari8Machine::Console:
            break;
    }
    if (releases_() && !hold_)
        add_pause_(run, Pause::Run);
    return wire::run_act(run);
}

proto::MailboxAct Atari8Sequencer::block_(ActBytes bytes) const noexcept {
    return infra::make<proto::MailboxAct>(fill_.block(bytes));
}

}
