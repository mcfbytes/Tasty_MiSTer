// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/sio_engine.h"

#include <algorithm>
#include <cassert>

#include "cores/atari8/atari8_decl.h"
#include "cores/atari8/atari8_wire.h"
#include "cores/atari8/sio_checksum.h"
#include "cores/atari8/sio_wire.h"
#include "infra/message_sum.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"

namespace mister::cores::atari8 {

namespace {

using wire::Level;
using wire::SioPort;
using wire::WriteRegister;

constexpr SioDivisor kResetDivisor = SioSpeedChoice::kStandard.divisor();

constexpr proto::ReplyCount kDrainPair{2};
constexpr proto::ReplyCount kMostPops = proto::user_io::kMaxPollFrameReplies;

}

void SioEngine::begin_core() noexcept {
    TASTY_SEAT_BODY(SioEngine);
    step_ = Step::Listen;
    staged_ = {};
    rx_known_ = {};
    reader_.reset();
    verb_.reset();
}

void SioEngine::order_reset() noexcept {
    TASTY_SEAT_BODY(SioEngine);
    stage_(Step::Divisor, {}, false);
    staged_.add(wire::divisor(kResetDivisor));
    verb_.reset();
}

void SioEngine::order_boot() noexcept {
    TASTY_SEAT_BODY(SioEngine);
    stage_(Step::BootLead, {}, false);
    staged_.add(wire::set(WriteRegister::TapeReset, Level::High));
    staged_.add(wire::set(WriteRegister::TapeReset, Level::Low));
}

bool SioEngine::wants_reboot() const noexcept {
    TASTY_SEAT_BODY(SioEngine);
    return step_ == Step::BootWait;
}

void SioEngine::reboot_ordered() noexcept {
    TASTY_SEAT_BODY(SioEngine);
    stage_(Step::BootTail, {}, false);
    staged_.add(wire::set(WriteRegister::OptionForce, Level::High));
    staged_.add(wire::set(WriteRegister::OptionForce, Level::Low));
}

void SioEngine::on_rx(SioRxStatus status) noexcept {
    TASTY_SEAT_BODY(SioEngine);
    rx_known_ = status.held();
}

SioEngine::Step SioEngine::step() const noexcept {
    TASTY_SEAT_BODY(SioEngine);
    return step_;
}

const SioFrame& SioEngine::frame() const noexcept {
    TASTY_SEAT_BODY(SioEngine);
    return frame_;
}

proto::MailboxAct SioEngine::next(ActBytes bytes, const SioTerms& t) {
    TASTY_SEAT_BODY(SioEngine);
    (void)t;
    switch (step_) {
        case Step::Listen:
            return rx_known_ == SioWordCount{} ? proto::MailboxAct{} : drain_act_(bytes);
        case Step::Receive:
            return rx_known_ == SioWordCount{} ? proto::MailboxAct{} : receive_act_(bytes);
        case Step::BootWait:
        case Step::Drain:
            return {};
        case Step::Switch:
        case Step::Ack:
        case Step::Complete:
        case Step::Data:
        case Step::WriteAck:
        case Step::WriteComplete:
        case Step::Tail:
        case Step::Divisor:
        case Step::BootLead:
        case Step::BootTail:
            return staged_act_(bytes);
    }
    return {};
}

void SioEngine::settled(const proto::MailboxResult& result, const SioTerms& t) {
    TASTY_SEAT_BODY(SioEngine);
    switch (step_) {
        case Step::Listen:
            drained_(result, t);
            return;
        case Step::Receive:
            received_(result, t);
            return;
        case Step::Drain:
        case Step::BootWait:
            return;
        case Step::Switch:
        case Step::Ack:
        case Step::Complete:
        case Step::Data:
        case Step::WriteAck:
        case Step::WriteComplete:
        case Step::Tail:
        case Step::Divisor:
        case Step::BootLead:
        case Step::BootTail:
            break;
    }

    staged_.advance(result.replies.kept);
    if (staged_.cursor != SioRunIndex{})
        staged_.not_before = {};
    if (staged_.done())
        staged_done_(t);
}

void SioEngine::stage_(Step step, Instant not_before, bool guarded) noexcept {
    step_ = step;
    staged_.size = {};
    staged_.cursor = {};
    staged_.not_before = not_before;
    staged_.guarded = guarded;
}

void SioEngine::transmit_(std::byte byte) noexcept {
    staged_.add(wire::transmit(byte));
}

proto::MailboxAct SioEngine::staged_act_(ActBytes bytes) noexcept {
    proto::MailboxRun run{bytes};
    for (SioRunIndex i = staged_.cursor; i < infra::affine::end_of<SioRunIndex>(staged_.size);
         i += SioRunCount{1})
        if (!run.add(staged_.commands[i]))
            break;
    return infra::make<proto::MailboxAct>(proto::MailboxAct::CommandRun{
        .not_before = staged_.not_before,
        .commands = run.commands(),
        .guard = staged_.guarded ? kTxGuard : proto::MailboxRunGuard::kNone
    });
}

proto::MailboxAct SioEngine::drain_act_(ActBytes bytes) noexcept {
    const SioWordCount wanted = reader_.wanted();
    pops_ = std::min(rx_known_, wanted).as_replies();
    asked_divisor_ = SioWordCount::of(pops_) == wanted;
    proto::MailboxRun run{bytes};
    for (proto::ReplyCount i{}; i < pops_; i += proto::ReplyCount{1}) {
        (void)run.add(wire::read(SioPort::Rx));
        (void)run.add(wire::read(SioPort::Error));
    }
    if (asked_divisor_)
        (void)run.add(wire::read(SioPort::Divisor));
    return wire::run_act(run);
}

void SioEngine::forget_rx_(SioWordCount popped) noexcept {
    rx_known_ = infra::affine::saturating_sub(rx_known_, popped);
}

void SioEngine::drained_(const proto::MailboxResult& result, const SioTerms& t) {
    const proto::user_io::PollFrame& r = result.replies;
    const proto::ReplyCount kept = r.kept;

    proto::ReplyIndex divisor_at{};
    for (proto::ReplyCount pop{}; pop < pops_; pop += proto::ReplyCount{1})
        divisor_at += kDrainPair;
    SioWordCount popped{};
    proto::ReplyIndex at{};
    for (proto::ReplyCount pop{}; pop < pops_ && infra::affine::in_bounds(kept, at);
         pop += proto::ReplyCount{1}, at += kDrainPair) {
        popped += SioWordCount{1};
        const SioRxWord word{r.replies[at]};
        const SioError error = infra::affine::in_bounds(kept, at + proto::ReplyCount{1})
            ? SioError{r.replies[at + proto::ReplyCount{1}]}
            : SioError{};
        if (reader_.feed(word, error) != SioFrameReader::Fed::Complete)
            continue;
        assert(at + kDrainPair == divisor_at);
        if (!asked_divisor_ || !infra::affine::in_bounds(kept, divisor_at))
            continue;
        forget_rx_(popped);
        frame_complete_(SioDivisor::switched_from(r.replies[divisor_at]), t);
        return;
    }
    forget_rx_(popped);
}

void SioEngine::frame_complete_(SioDivisor divisor, const SioTerms& t) {
    frame_ = reader_.frame();
    unit_ = SioDrives::unit_of(frame_.device);
    verb_.reset();
    const bool served = t.drives.serves_serial(frame_);
    stage_(Step::Switch, {}, false);
    staged_.add(wire::divisor(divisor));
    if (!served)
        return;
    staged_.add(wire::set(WriteRegister::DriveLed, Level::High));
    verb_ = t.drives.verb_for(frame_, call_(t));
}

SioCall SioEngine::call_(const SioTerms& t) const noexcept {
    return SioCall{.bus = SioBus::Serial, .status = t.status, .now = t.now};
}

void SioEngine::staged_done_(const SioTerms& t) {
    switch (step_) {
        case Step::Switch: {

            if (staged_.size == SioRunCount{1}) {
                step_ = Step::Listen;
                return;
            }
            const bool timed_read = verb_ == SioCommand::Read && t.drives.timed(unit_);
            stage_(Step::Ack, t.now + (timed_read ? kPreAckTimed : kPreAck), true);
            transmit_(verb_ ? wire::kAck : wire::kNak);
            return;
        }
        case Step::Ack:
            if (!verb_) {
                stage_tail_();
                return;
            }
            if (*verb_ == SioCommand::Write || *verb_ == SioCommand::WriteVerify) {
                step_ = Step::Receive;
                expected_ = t.drives.write_bytes(frame_, unit_);
                received_bytes_ = {};
                std::ranges::fill(data_, std::byte{});
                return;
            }
            run_verb_(t);
            return;
        case Step::Complete:
            stage_data_(t);
            return;
        case Step::WriteAck: {
            reply_ = t.drives.execute(*verb_, frame_, unit_, call_(t), buffer_);
            const std::chrono::microseconds busy = reply_.timed.value_or(
                std::chrono::microseconds{}
            );
            stage_(Step::WriteComplete, t.now + busy + kPreComplete, true);
            transmit_(reply_.success ? wire::kComplete : wire::kError);
            return;
        }
        case Step::Data:
        case Step::WriteComplete:
            stage_tail_();
            return;
        case Step::BootLead:
            step_ = Step::BootWait;
            return;
        case Step::Tail:
        case Step::Divisor:
        case Step::BootTail:
        case Step::Listen:
        case Step::Drain:
        case Step::Receive:
        case Step::BootWait:
            step_ = Step::Listen;
            return;
    }
}

void SioEngine::run_verb_(const SioTerms& t) {
    std::ranges::fill(buffer_, std::byte{});
    reply_ = t.drives.execute(*verb_, frame_, unit_, call_(t), buffer_);
    if (!reply_.respond) {
        stage_tail_();
        return;
    }

    stage_(Step::Complete, t.now + reply_.timed.value_or(kPreComplete), true);
    transmit_(reply_.success ? wire::kComplete : wire::kError);
}

void SioEngine::stage_data_(const SioTerms& t) noexcept {
    stage_(Step::Data, t.now + kPreData, true);
    const std::span<const std::byte> data = reply_.bytes.first_in(
        std::span<const std::byte>{buffer_}
    );
    for (const std::byte b : data)
        transmit_(b);
    transmit_(sio_checksum(data));
}

void SioEngine::stage_tail_() noexcept {
    stage_(Step::Tail, {}, false);
    if (verb_ && reply_.divisor)
        staged_.add(wire::divisor(*reply_.divisor));
    staged_.add(wire::set(WriteRegister::DriveLed, Level::Low));
}

proto::MailboxAct SioEngine::receive_act_(ActBytes bytes) noexcept {
    const infra::ByteCount remaining = expected_ + infra::ByteCount{1} - received_bytes_;
    pops_ = std::min({rx_known_, SioWordCount::at_most(remaining), SioWordCount::of(kMostPops)})
                .as_replies();
    proto::MailboxRun run{bytes};
    for (proto::ReplyCount i{}; i < pops_; i += proto::ReplyCount{1})
        (void)run.add(wire::read(SioPort::Rx));
    return wire::run_act(run);
}

void SioEngine::received_(const proto::MailboxResult& result, const SioTerms& t) {
    const proto::user_io::PollFrame& r = result.replies;
    const proto::ReplyCount got = std::min(r.kept, pops_);
    for (proto::ReplyIndex i{}; infra::affine::in_bounds(got, i); i += proto::ReplyCount{1}) {
        if (!(received_bytes_ < infra::ByteCount{data_.size()}))
            break;
        data_[infra::affine::end_of<infra::ByteOffset>(received_bytes_)] = SioRxWord{r.replies[i]}
                                                                               .byte();
        received_bytes_ += infra::ByteCount{1};
    }
    forget_rx_(SioWordCount::of(got));
    if (received_bytes_ < expected_ + infra::ByteCount{1})
        return;
    const std::span<const std::byte> sector = data_.prefix(expected_);
    const bool summed = sio_checksum(sector)
        == data_[infra::affine::end_of<infra::ByteOffset>(expected_)];
    std::ranges::fill(buffer_, std::byte{});
    std::ranges::copy(sector, buffer_.begin());
    if (!summed) {
        verb_.reset();
        stage_(Step::Ack, {}, true);
        transmit_(wire::kNak);
        return;
    }
    stage_(Step::WriteAck, t.now + kPreWriteAck, true);
    transmit_(wire::kAck);
}

}
