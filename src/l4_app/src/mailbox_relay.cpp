// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/mailbox_relay.h"

#include <algorithm>
#include <array>
#include <span>

#include "hal/selected.h"
#include "hal/spi_transport.h"
#include "proto/spi_fio_queue.h"

namespace mister::app {

void MailboxRelay::bind(proto::MailboxPoll poll, std::uint16_t gen) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(MailboxRelay, A);
    gen_ = gen;
    if (poll != proto::MailboxPoll::Keep) polling_ = poll == proto::MailboxPoll::On;
}

void MailboxRelay::forget() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(MailboxRelay, A);
    polling_ = false;
    gen_ = 0;
    have_last_ = false;
}

void MailboxRelay::service_rt(hal::ISpiTransport& link, proto::SpiFioQueue& queue,
                              const proto::MailboxFrameDecl& frame) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(MailboxRelay, A);
    ++round_;
    const std::size_t widest = queue.bracket_windows(proto::kMailboxActBytes);
    for (unsigned i = 0; i < kReapPerRound; ++i) {
        if (!queue.fits(widest, proto::kMailboxActBytes, 1)) break;
        const Chan::Loan done = chan_.reap();
        if (!done) break;
        if (!done.completed() || done->gen != gen_ || gen_ == 0) {
            stale_.add();
            have_last_ = false;
            continue;
        }
        write_(queue, *done);
    }
    if (!polling_ || !proto::well_formed(frame)) return;
    proto::MailboxFrame f{};
    bool moved = true;
    {
        hal::Selected cs(link, hal::ChipSelect::Io);
        for (std::uint8_t i = 0; i < frame.words && moved; ++i) {
            const auto r =
                link.transfer(hal::SpiWord{i == 0 ? frame.poll_opcode : std::uint16_t{0}});
            if (!r) {
                wire_faults_.add();
                return;
            }
            f.w[i] = r->v;
            if (i == 0) moved = !have_last_ || f.w[0] != last_head_;
        }
    }
    polls_.add();
    if (!moved) return;
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
    have_last_ = true;
    forwarded_.add();
}

void MailboxRelay::write_(proto::SpiFioQueue& queue, const MailboxSlot& s) noexcept {
    using Kind = proto::MailboxAct::Kind;
    const proto::MailboxAct& a = s.act;
    if (a.kind == Kind::None) return;
    bool ok = false;
    if (a.kind == Kind::Command) {
        const std::array<std::uint16_t, 4> w{a.opcode, a.words[0], a.words[1], a.words[2]};
        ok = queue.append_command(w).has_value();
    } else {
        const std::size_t n = std::min<std::size_t>(a.len, s.bytes.size());
        const proto::SpiFioQueue::Bracket b[] = {{a.index, std::span(s.bytes).first(n)}};
        ok = queue.append(b).has_value();
    }
    if (ok) {
        acts_.add();
    } else {
        refusals_.add();
    }
}

void MailboxRelay::serve_worker(CompanionHost& host) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(MailboxRelay, B);
    while (Chan::Job job = chan_.take()) {
        cores::IMailboxServant* s = host.servant_for(job->gen);
        job->act = s != nullptr ? s->serve(job->frame, std::span(job->bytes)) : proto::MailboxAct{};
        job.complete();
    }
}

void MailboxRelay::park_now() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(MailboxRelay, B);
    while (Chan::Job job = chan_.take()) {
        job->act = {};
        job.complete();
    }
}

}  // namespace mister::app
