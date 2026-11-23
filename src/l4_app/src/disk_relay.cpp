// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/disk_relay.h"

#include <concepts>
#include <span>
#include <utility>

#include "hal/spi_halves.h"
#include "hal/spi_transport.h"
#include "infra/message_sum.h"
#include "infra/prelude.h"
#include "proto/link/link.h"
#include "proto/link/poll_frame_read.h"

namespace mister::app {

namespace {

using Ask = DiskSlot::Ask;

[[nodiscard]] Ask ask_of(const auto& alt, RelayStamp stamp) noexcept {
    return infra::make<Ask>(alt, stamp);
}

struct HeldLegs {
    template <class A>
    static constexpr bool routed = std::same_as<A, Ask::Command>
        || std::same_as<A, Ask::Resume>
        || std::same_as<A, Ask::Attach>;
};

}

void DiskRelay::service_rt(proto::link::Link& link, const proto::DiskPollProgram& poll) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    const bool was_open = rt_.command_open;
    if (Chan::Loan l = chan_.reap())
        land_(std::move(l), &link);
    if (!send_orders_())
        return;
    if (was_open || rt_.command_open)
        return;
    if (poll_(link, poll))
        write_standing_(link);
}

proto::FioWordCount DiskRelay::take_round_words() noexcept {
    return rt_.round_words.take();
}

infra::OptRef<DiskRelay::Order> DiskRelay::order_of_(proto::DiskUnit unit) noexcept {
    if (!row_.units.contains(unit))
        return {};
    return rt_.orders.find(unit);
}

void DiskRelay::order_bind(proto::DiskUnit unit, std::string_view path) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    const infra::OptRef<Order> order = order_of_(unit);
    if (!order)
        return;
    Order& o = *order;
    do
        ++o.seq;
    while (o.seq == 0);
    o.answer = cores::MountState::Pending;
    o.sent = false;
    if (!o.path.assign(path)) {
        o.path.clear();
        o.sent = true;
        o.answered = o.seq;
        o.answer = cores::MountState::Failed;
        return;
    }
    (void)send_orders_();
}

cores::MountState DiskRelay::bind_answer(proto::DiskUnit unit) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    const infra::OptRef<Order> order = order_of_(unit);
    if (!order)
        return cores::MountState::Failed;
    const Order& o = *order;
    if (o.seq != 0 && o.answered != o.seq) {
        (void)send_orders_();
        if (!rt_.command_open) {
            if (Chan::Loan l = chan_.reap())
                land_(std::move(l), nullptr);
        }
    }
    return o.seq != 0 && o.answered == o.seq ? o.answer : cores::MountState::Pending;
}

void DiskRelay::abandon_command() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    if (!rt_.command_open)
        return;
    rt_.stamp = rt_.stamp.next_epoch();
    close_command_();
    rt_.abandoned.add();
}

void DiskRelay::abandon_command_on(proto::DiskUnit unit) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    if (rt_.open_unit.has_value() && *rt_.open_unit != unit)
        return;
    abandon_command();
}

void DiskRelay::attach(DiskAttachSeq order, infra::OptRef<cores::IDiskServant> servant) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    rt_.stamp = rt_.stamp.next_core();
    close_command_();
    rt_.orders = {};
    rt_.last_seen.reset();
    rt_.standing = proto::DiskAct::none();
    while (Chan::Loan l = chan_.reap())
        land_(std::move(l), nullptr);
    Attachment& at = rt_.attach;
    at.servant = servant;
    at.ordered = order;
    at.owed = true;

    if (!servant && !at.lent) {
        at.owed = false;
        at.home = order;
        return;
    }
    (void)send_orders_();
}

bool DiskRelay::settle_attach(DiskAttachSeq order) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    if (rt_.attach.home.at_or_past(order))
        return true;

    while (!rt_.command_open) {
        Chan::Loan l = chan_.reap();
        if (!l)
            break;
        land_(std::move(l), nullptr);
    }
    (void)send_orders_();
    return rt_.attach.home.at_or_past(order);
}

void DiskRelay::close_command_() noexcept {
    rt_.command_open = false;
    rt_.open_unit.reset();
}

void DiskRelay::close_stale_command_(const RelayStamp& h) noexcept {
    if (!h.matches(rt_.stamp))
        return;
    close_command_();
    rt_.last_seen.reset();
}

void DiskRelay::owe_attach_again_(DiskAttachSeq order) noexcept {
    if (order == rt_.attach.ordered)
        rt_.attach.owed = true;
}

void DiskRelay::land_(Chan::Loan&& l, proto::link::Link* link) noexcept {
    if (!l.completed()) {
        rt_.stale.add();
        StaleLanding stale{*this};
        infra::dispatch<HeldLegs>(l->ask, stale);
        return;
    }
    infra::dispatch<infra::AllRouted>(l->ask, *this, l, link);
}

void DiskRelay::on(
    const Ask::Bind& a,
    const RelayStamp& h,
    Chan::Loan& l,
    proto::link::Link*
) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    const infra::OptRef<Order> order = order_of_(a.unit);
    if (!h.same_core(rt_.stamp) || !order) {
        rt_.stale.add();
        return;
    }

    rt_.standing = l->act;
    Order& o = *order;
    if (a.seq != o.seq)
        return;
    o.answered = a.seq;
    o.answer = l->bound;
}

void DiskRelay::on(
    const Ask::Command&,
    const RelayStamp& h,
    Chan::Loan& l,
    proto::link::Link* link
) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    act_(h, l, link);
}

void DiskRelay::on(
    const Ask::Resume&,
    const RelayStamp& h,
    Chan::Loan& l,
    proto::link::Link* link
) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    act_(h, l, link);
}

void DiskRelay::on(
    const Ask::Attach& a,
    const RelayStamp&,
    Chan::Loan& l,
    proto::link::Link*
) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    Attachment& at = rt_.attach;
    if (a.order == at.ordered)
        rt_.standing = l->act;
    if (a.order.at_or_past(at.home))
        at.home = a.order;
    if (!a.servant && a.order == at.sent)
        at.lent = false;
}

void DiskRelay::misrouted(const Ask&, Chan::Loan&, proto::link::Link*) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    rt_.stale.add();
}

void DiskRelay::act_(const RelayStamp& h, Chan::Loan& l, proto::link::Link* link) noexcept {
    if (!h.matches(rt_.stamp)) {
        rt_.stale.add();
        return;
    }
    const proto::DiskAct a = l->act;
    if (const std::optional<proto::DiskUnit> u = a.unit())
        rt_.open_unit = *u;
    if (a.kind() == proto::DiskAct::Kind::None) {
        close_command_();
        return;
    }
    const Written written = link == nullptr ? Written::Faulted : write_(*link, a, l->bytes);
    switch (written) {
        case Written::Whole:
            break;
        case Written::Dropped:
            rt_.dropped.add();
            close_command_();
            return;
        case Written::Faulted:
            rt_.wire_faults.add();
            close_command_();
            return;
    }
    rt_.acts.add();
    if (a.last()) {
        close_command_();
        return;
    }
    l->ask = ask_of(Ask::Resume{}, rt_.stamp);
    l->act = {};
    chan_.send(std::move(l));
}

DiskRelay::Written DiskRelay::write_(
    proto::link::Link& link,
    const proto::DiskAct& a,
    std::span<std::byte> bytes
) noexcept {
    const bool block = a.kind() == proto::DiskAct::Kind::Out
        || a.kind() == proto::DiskAct::Kind::In;
    const proto::ReplyCount reads = block ? row_.status_reads : proto::ReplyCount{};

    if ((a.len() & 1u) != 0u || a.len() > bytes.size())
        return Written::Faulted;
    if (a.address() && reads != proto::ReplyCount{})
        return Written::Faulted;

    rt_.round_words.charge(a.words() + reads.link_words());
    const auto beat = link.command(row_.select, a.opcode());
    if (!beat)
        return Written::Faulted;
    if (const std::optional<proto::DiskAddress> address = a.address()) {
        const hal::SpiHalves at = address->halves();
        if (!link.word(at.low) || !link.word(at.high))
            return Written::Faulted;
    }
    for (proto::ReplyCount n{}; n < reads; n += proto::ReplyCount{1}) {
        if (!link.read())
            return Written::Faulted;
    }

    if (const std::optional<proto::DiskStatusTag> tag = a.tag(); tag && !tag->holds(*beat)) {
        link.close();
        return Written::Dropped;
    }
    switch (a.kind()) {
        case proto::DiskAct::Kind::Out:
            for (std::size_t i = 0; i < a.len(); i += proto::DiskAct::kWordBytes) {
                const auto word = hal::SpiWord::load_le(
                    std::span<const std::byte>(bytes).subspan(i).first<proto::DiskAct::kWordBytes>()
                );
                if (!link.word(word))
                    return Written::Faulted;
            }
            break;
        case proto::DiskAct::Kind::In:
            for (std::size_t i = 0; i < a.len(); i += proto::DiskAct::kWordBytes) {
                const auto r = link.read();
                if (!r)
                    return Written::Faulted;
                r->store_le(bytes.subspan(i).first<proto::DiskAct::kWordBytes>());
            }
            break;
        case proto::DiskAct::Kind::Signal: {
            const std::optional<hal::SpiWord> w = a.word();
            if (w.has_value() && !link.word(*w))
                return Written::Faulted;
            break;
        }
        case proto::DiskAct::Kind::None:
            break;
    }
    link.close();
    return Written::Whole;
}

void DiskRelay::write_standing_(proto::link::Link& link) noexcept {
    if (rt_.standing.kind() != proto::DiskAct::Kind::Signal)
        return;
    if (write_(link, rt_.standing, {}) != Written::Whole)
        rt_.wire_faults.add();
}

bool DiskRelay::send_attach_() noexcept {
    Attachment& at = rt_.attach;
    if (!at.owed)
        return true;
    Chan::Loan l = chan_.acquire();
    if (!l)
        return false;
    l->ask = ask_of(Ask::Attach{.servant = at.servant, .order = at.ordered}, rt_.stamp);
    l->act = {};
    chan_.send(std::move(l));
    at.owed = false;
    at.sent = at.ordered;
    if (at.servant)
        at.lent = true;
    return true;
}

bool DiskRelay::send_orders_() noexcept {
    if (!send_attach_())
        return false;
    proto::DiskUnits units = row_.units;
    while (const std::optional<proto::DiskUnit> unit = units.pop_lowest()) {
        if (!send_order_(*unit))
            return false;
    }
    return true;
}

bool DiskRelay::send_order_(proto::DiskUnit unit) noexcept {
    Order& o = rt_.orders[unit];
    if (o.seq == 0 || o.sent)
        return true;
    Chan::Loan l = rt_.command_open ? Chan::Loan{} : chan_.acquire();
    if (!l)
        return false;
    Ask::Bind b{.unit = unit, .seq = o.seq};
    (void)b.path.assign(o.path.view());
    l->ask = ask_of(b, rt_.stamp);
    l->act = {};
    l->bound = cores::MountState::Pending;
    chan_.send(std::move(l));
    o.sent = true;
    return true;
}

bool DiskRelay::poll_(proto::link::Link& link, const proto::DiskPollProgram& poll) noexcept {
    using Outcome = proto::link::PollFrameRead::Outcome;
    const proto::user_io::PollFrameShape& head = poll.head();
    const proto::link::PollFrameRead first = link.read_poll_frame(head, rt_.last_seen);
    if (first.outcome == Outcome::Faulted) {
        rt_.wire_faults.add();
        return false;
    }
    rt_.polls.add();
    if (first.outcome == Outcome::Held)
        return true;
    Ask::Command c{.block = first.frame};
    const proto::ReplyWord asked = head.guard_word(first.frame);
    for (const proto::DiskPollStep& step : poll.gated()) {
        if (!asked.intersects(step.gate)) {
            c.block.keep_blank(step.frame.kept());
            continue;
        }
        const proto::link::PollFrameRead more = link.read_poll_frame(step.frame, std::nullopt);
        if (more.outcome == Outcome::Faulted) {
            rt_.wire_faults.add();
            return false;
        }
        c.block.keep_all(more.frame);
    }
    Chan::Loan l = chan_.acquire();
    if (!l) {
        rt_.declines.add();
        return true;
    }
    l->ask = ask_of(c, rt_.stamp);
    l->act = {};
    chan_.send(std::move(l));
    rt_.command_open = true;
    rt_.last_seen = asked;
    rt_.lent.add();
    return true;
}

void DiskRelay::serve() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    while (Job job = chan_.take()) {
        infra::dispatch<infra::AllRouted>(job->ask, *this, job);
        job.complete();
    }
}

bool DiskRelay::serves_(const RelayStamp& h) const noexcept {
    return io_.servant.has_value() && h.same_core(io_.served);
}

void DiskRelay::on(const Ask::Bind& a, const RelayStamp& h, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    job->bound = serves_(h) ? io_.servant->bind(a.unit, a.path.view()) : cores::MountState::Failed;
    job->act = serves_(h) ? io_.servant->standing() : proto::DiskAct::none();
}

void DiskRelay::on(const Ask::Command& a, const RelayStamp& h, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    job->act = serves_(h) ? io_.servant->start(a.block, job->bytes) : proto::DiskAct::none();
}

void DiskRelay::on(const Ask::Resume&, const RelayStamp& h, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    job->act = serves_(h) ? io_.servant->resume(job->bytes) : proto::DiskAct::none();
}

void DiskRelay::on(const Ask::Attach& a, const RelayStamp& h, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    if (io_.servant)
        io_.servant->release();
    io_.servant = a.servant;
    io_.served = h;
    job->act = io_.servant ? io_.servant->standing() : proto::DiskAct::none();
}

void DiskRelay::misrouted(const Ask&, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    job->act = proto::DiskAct::none();
}

}
