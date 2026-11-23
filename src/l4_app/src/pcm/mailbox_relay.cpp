// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/pcm/mailbox_relay.h"

#include "app/pcm/mailbox_act_writer.h"

#include <algorithm>
#include <span>
#include <utility>

#include "hal/selected.h"
#include "hal/spi_transport.h"
#include "infra/message_sum.h"
#include "proto/link/poll_frame_read.h"
#include "proto/link/spi_fio_queue.h"
#include "proto/uio_opcodes.h"

namespace mister::app::pcm {

void MailboxRelay::bind(proto::MailboxPoll poll, proto::CompanionBindGen gen) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(MailboxRelay, A);
    gen_ = gen;
    bound_afresh_ = true;
    if (poll != proto::MailboxPoll::Keep)
        polling_ = poll == proto::MailboxPoll::On;
}

void MailboxRelay::forget() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(MailboxRelay, A);
    polling_ = false;
    gen_ = proto::CompanionBindGen::none();
    mirrors_ = {};
    bound_afresh_ = false;
    held_act_.reset();
}

void MailboxRelay::service_rt(
    proto::link::Link& link,
    proto::link::SpiFioQueue& queue,
    const proto::MailboxProgramme& programme,
    const os::IClock& clock
) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(MailboxRelay, A);
    ++round_;
    const RoundActs acted = write_acts_(link, queue, programme, clock);
    if (polling_)
        run_programme_(link, programme, acted);
}

MailboxRelay::RoundActs MailboxRelay::write_acts_(
    proto::link::Link& link,
    proto::link::SpiFioQueue& queue,
    const proto::MailboxProgramme& programme,
    const os::IClock& clock
) noexcept {
    const std::size_t widest = queue.bracket_windows(proto::kMailboxActBytes);
    const proto::link::SpiFioQueue::ActShape ask{
        .windows = proto::WindowCount{widest},
        .bytes = proto::FileSize{proto::kMailboxActBytes},
        .brackets = proto::BracketCount{1}
    };
    RoundActs acted{};
    bool bus_spent = false;
    for (unsigned i = 0; i < kReapPerRound; ++i) {
        if (!queue.fits(ask))
            break;
        Chan::Loan done;
        if (held_act_) {
            done = std::move(*held_act_);
            held_act_.reset();
        } else {
            done = chan_.reap();
        }
        if (!done)
            break;
        if (!done.completed() || done->gen != gen_ || gen_.is_none()) {
            stale_.add();

            if (const auto mirror = mirrors_.find(done->select))
                mirror->reset();
            continue;
        }
        detail::ActWriter w{
            link,
            queue,
            programme,
            clock,
            *done,
            round_words_,
            acted.direct_spent,
            bus_spent,
            acted.wrote,
            acts_,
            refusals_,
            none_acts_
        };
        const detail::ActEnd end = infra::dispatch<infra::AllRouted>(done->act, w);
        if (end == detail::ActEnd::Held) {
            held_act_ = std::move(done);
            return acted;
        }
        if (end == detail::ActEnd::Reported) {
            done->origin = MailboxSlot::Origin::Result;
            done->act = {};
            chan_.send(std::move(done));
        }
    }
    return acted;
}

void MailboxRelay::run_programme_(
    proto::link::Link& link,
    const proto::MailboxProgramme& programme,
    RoundActs acted
) noexcept {
    infra::IndexedArray<bool, proto::kMailboxSelects.size(), proto::MailboxSelectIndex> lent{};
    Mirrors read{};
    proto::MailboxSelectIndex next{};
    const bool afresh = std::exchange(bound_afresh_, false);
    for (const proto::MailboxSelect& select : programme.polls()) {
        const proto::MailboxSelectIndex self = next;
        next = next.next();
        if (afresh && select.relent_at_bind)
            mirrors_[self].reset();
        if (acted.wrote && select.relent_after_act)
            mirrors_[self].reset();
        if (select.follows && !lent[*select.follows])
            continue;
        if (select.gate && !select.gate->holds(read[select.gate->on]))
            continue;

        Chan::Loan l = select.pops ? chan_.acquire() : Chan::Loan{};
        if (select.pops && !l) {
            declines_.add();
            continue;
        }
        const proto::link::PollFrameRead first = link.read_poll_frame(select.shape, mirrors_[self]);
        round_words_.charge(first.charged);
        if (first.outcome == proto::link::PollFrameRead::Outcome::Faulted) {
            wire_faults_.add();
            return;
        }
        polls_.add();
        read[self] = select.shape.guard_word(first.frame);
        if (first.outcome == proto::link::PollFrameRead::Outcome::Held)
            continue;
        proto::user_io::PollFrame frame = first.frame;
        if (select.repeats != proto::ReplyCount{}
            && !acted.direct_spent
            && !repeat_(link, programme, select, frame))
            return;
        if (!l)
            l = chan_.acquire();
        if (!l) {
            declines_.add();
            continue;
        }
        l->gen = gen_;
        l->round = round_;
        l->origin = MailboxSlot::Origin::Poll;
        l->select = self;
        l->frame = frame;
        l->act = {};
        chan_.send(std::move(l));
        mirrors_[self] = select.shape.guard_word(first.frame);
        forwarded_.add();
        lent[self] = true;
    }
}

bool MailboxRelay::repeat_(
    proto::link::Link& link,
    const proto::MailboxProgramme& programme,
    const proto::MailboxSelect& select,
    proto::user_io::PollFrame& frame
) noexcept {
    const proto::MailboxSelect& gate = select.gate->on.in(programme.polls());
    for (proto::ReplyCount done{}; done < select.repeats; done += proto::ReplyCount{1}) {
        if (frame.kept + select.shape.kept() > proto::user_io::kMaxPollFrameReplies)
            return true;
        const proto::link::PollFrameRead asked = link.read_poll_frame(gate.shape, std::nullopt);
        round_words_.charge(asked.charged);
        if (asked.outcome == proto::link::PollFrameRead::Outcome::Faulted)
            return faulted_();
        if (!select.gate->holds(gate.shape.guard_word(asked.frame)))
            return true;
        const proto::link::PollFrameRead more = link.read_poll_frame(select.shape, std::nullopt);
        round_words_.charge(more.charged);
        if (more.outcome == proto::link::PollFrameRead::Outcome::Faulted)
            return faulted_();
        frame.keep_all(more.frame);
    }
    return true;
}

bool MailboxRelay::faulted_() noexcept {
    wire_faults_.add();
    return false;
}

proto::MailboxAct MailboxRelay::answer_(
    cores::IMailboxServant& servant,
    MailboxSlot& slot
) noexcept {
    switch (slot.origin) {
        case MailboxSlot::Origin::Poll:
            return servant
                .serve(slot.select, slot.frame, std::span(slot.bytes), std::span(slot.bulk));
        case MailboxSlot::Origin::Result:
            return servant.settle(slot.result, std::span(slot.bytes), std::span(slot.bulk));
    }
    return {};
}

void MailboxRelay::serve_worker(CompanionHost& host) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(MailboxRelay, B);
    for (;;) {
        if (!held_) {
            Chan::Job job = chan_.take();
            if (!job)
                return;
            held_ = std::move(job);
            held_awaited_ = false;
        }

        const auto s = host.servant_for((**held_).gen);
        if (!s && host.awaits_bind((**held_).gen) && !held_awaited_) {
            held_awaited_ = true;
            return;
        }
        (**held_).act = s ? answer_(*s, **held_) : proto::MailboxAct{};
        held_->complete();
        held_.reset();
        held_awaited_ = false;
    }
}

void MailboxRelay::park_now() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(MailboxRelay, B);
    if (held_) {
        (**held_).act = {};
        held_->complete();
        held_.reset();
        held_awaited_ = false;
    }
    while (Chan::Job job = chan_.take()) {
        job->act = {};
        job.complete();
    }
}

}
