// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/mailbox_relay.h"

#include "app/mailbox_act_writer.h"

#include <algorithm>
#include <span>

#include "hal/selected.h"
#include "hal/spi_transport.h"
#include "infra/message_sum.h"
#include "proto/spi_fio_queue.h"
#include "proto/uio_opcodes.h"

namespace mister::app {

void MailboxRelay::bind(proto::MailboxPoll poll, proto::CompanionBindGen gen) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(MailboxRelay, A);
    gen_ = gen;
    if (poll != proto::MailboxPoll::Keep)
        polling_ = poll == proto::MailboxPoll::On;
}

void MailboxRelay::forget() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(MailboxRelay, A);
    polling_ = false;
    gen_ = proto::CompanionBindGen::none();
    last_head_.reset();
}

void MailboxRelay::service_rt(
    proto::Link& link,
    proto::SpiFioQueue& queue,
    const proto::MailboxFrameDecl& frame
) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(MailboxRelay, A);
    ++round_;
    const std::size_t widest = queue.bracket_windows(proto::kMailboxActBytes);
    const proto::SpiFioQueue::ActShape ask{
        .windows = proto::WindowCount{widest},
        .bytes = proto::FileSize{proto::kMailboxActBytes},
        .brackets = proto::BracketCount{1}
    };
    for (unsigned i = 0; i < kReapPerRound; ++i) {
        if (!queue.fits(ask))
            break;
        const Chan::Loan done = chan_.reap();
        if (!done)
            break;
        if (!done.completed() || done->gen != gen_ || gen_.is_none()) {
            stale_.add();
            last_head_.reset();
            continue;
        }
        write_(queue, *done);
    }
    if (!polling_ || !proto::well_formed(frame))
        return;
    proto::MailboxFrame f{};
    bool moved = true;
    {

        const auto op = link.command(frame.poll_opcode);
        if (!op) {
            wire_faults_.add();
            return;
        }
        f.w[0] = *op;
        moved = !last_head_ || f.w[0] != *last_head_;
        for (std::uint8_t i = 1; i < frame.words && moved; ++i) {
            const auto r = link.read();
            if (!r) {
                wire_faults_.add();
                return;
            }
            f.w[i] = *r;
        }
        link.close();
    }
    polls_.add();
    if (!moved)
        return;
    Chan::Loan l = chan_.acquire();
    if (!l) {
        declines_.add();
        return;
    }
    l->gen = gen_;
    l->round = round_;
    l->frame = f;
    l->act = {};
    chan_.send(std::move(l));
    last_head_ = f.w[0];
    forwarded_.add();
}

void MailboxRelay::write_(proto::SpiFioQueue& queue, const MailboxSlot& s) noexcept {
    detail::ActWriter
        w{queue, std::span<const std::uint8_t>(s.bytes), acts_, refusals_, none_acts_};
    infra::dispatch<infra::AllRouted>(s.act, w);
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
        (**held_).act = s ? s->serve((**held_).frame, std::span((**held_).bytes))
                          : proto::MailboxAct{};
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
