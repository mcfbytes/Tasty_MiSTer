// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/disk_relay.h"
#include "proto/link.h"

#include <concepts>
#include <span>
#include <utility>

#include "hal/selected.h"
#include "hal/spi_transport.h"
#include "infra/endian.h"
#include "infra/message_sum.h"

namespace mister::app {

namespace {

using Ask = DiskSlot::Ask;

[[nodiscard]] Ask ask_of(const auto& alt, RelayStamp stamp) noexcept {
    return infra::make<Ask>(alt, stamp);
}

struct CommandLegs {
    template <class A>
    static constexpr bool routed = std::same_as<A, Ask::Command> || std::same_as<A, Ask::Resume>;
};

}

void DiskRelay::service_rt(proto::Link& link, const proto::DiskPollDecl& poll) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    const bool was_open = rt_.command_open;
    if (Chan::Loan l = chan_.reap())
        land_(std::move(l), &link);
    if (!send_orders_())
        return;
    if (was_open || rt_.command_open)
        return;
    poll_(link, poll);
}

void DiskRelay::order_bind(proto::DiskUnit unit, std::string_view path) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    const infra::OptRef<Order> order = rt_.orders.find(unit);
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
    const auto& orders = rt_.orders;
    const infra::OptRef<const Order> order = orders.find(unit);
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

void DiskRelay::forget() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    rt_.stamp = rt_.stamp.next_core();
    close_command_();
    rt_.orders = {};

    while (Chan::Loan l = chan_.reap())
        rt_.stale.add();
    rt_.release_owed = true;
    (void)send_orders_();
}

void DiskRelay::close_command_() noexcept {
    rt_.command_open = false;
    rt_.open_unit.reset();
}

void DiskRelay::land_(Chan::Loan&& l, proto::Link* link) noexcept {
    if (!l.completed()) {
        rt_.stale.add();
        if (l->ask.head.matches(rt_.stamp)) {
            StaleLanding stale{*this};
            infra::dispatch<CommandLegs>(l->ask, stale);
        }
        return;
    }
    infra::dispatch<infra::AllRouted>(l->ask, *this, l, link);
}

void DiskRelay::on(const Ask::Bind& a, const RelayStamp& h, Chan::Loan& l, proto::Link*) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    const infra::OptRef<Order> order = rt_.orders.find(a.unit);
    if (!h.same_core(rt_.stamp) || !order) {
        rt_.stale.add();
        return;
    }
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
    proto::Link* link
) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    act_(h, l, link);
}

void DiskRelay::on(
    const Ask::Resume&,
    const RelayStamp& h,
    Chan::Loan& l,
    proto::Link* link
) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    act_(h, l, link);
}

void DiskRelay::on(const Ask::Release&, const RelayStamp&, Chan::Loan&, proto::Link*) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
}

void DiskRelay::misrouted(const Ask&, Chan::Loan&, proto::Link*) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    rt_.stale.add();
}

void DiskRelay::act_(const RelayStamp& h, Chan::Loan& l, proto::Link* link) noexcept {
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
    if (link == nullptr || !write_(*link, a, *l)) {
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

bool DiskRelay::write_(proto::Link& link, const proto::DiskAct& a, DiskSlot& s) noexcept {

    if ((a.len() & 1u) != 0u || a.len() > s.bytes.size())
        return false;
    if (!link.command(hal::ChipSelect::Io, a.opcode()))
        return false;

    constexpr std::size_t kWordBytes = 2;
    constexpr unsigned kByteBits = 8;
    constexpr unsigned kByteMask = 0xFF;
    switch (a.kind()) {
        case proto::DiskAct::Kind::Out:
            for (std::size_t i = 0; i < a.len(); i += kWordBytes) {
                const auto word = infra::load_le<std::uint16_t>(
                    std::span(s.bytes).subspan(i).first<kWordBytes>()
                );
                if (!link.word(word))
                    return false;
            }
            break;
        case proto::DiskAct::Kind::In:
            for (std::size_t i = 0; i < a.len(); i += kWordBytes) {
                const auto r = link.read();
                if (!r)
                    return false;
                s.bytes[i] = static_cast<std::byte>(*r & kByteMask);
                s.bytes[i + 1] = static_cast<std::byte>((*r >> kByteBits) & kByteMask);
            }
            break;
        case proto::DiskAct::Kind::Signal: {
            const std::optional<std::uint16_t> w = a.word();
            if (w.has_value() && !link.word(*w))
                return false;
            break;
        }
        case proto::DiskAct::Kind::None:
            break;
    }
    link.close();
    return true;
}

bool DiskRelay::send_orders_() noexcept {
    if (rt_.release_owed) {
        Chan::Loan l = chan_.acquire();
        if (!l)
            return false;
        l->ask = ask_of(Ask::Release{}, rt_.stamp);
        l->act = {};
        chan_.send(std::move(l));
        rt_.release_owed = false;
    }
    for (std::uint8_t i = 0; i < kUnits; ++i) {
        const proto::DiskUnit unit{i};
        Order& o = rt_.orders[unit];
        if (o.seq == 0 || o.sent)
            continue;
        if (rt_.command_open)
            return false;
        Chan::Loan l = chan_.acquire();
        if (!l)
            return false;
        Ask::Bind b{.unit = unit, .seq = o.seq};
        (void)b.path.assign(o.path.view());
        l->ask = ask_of(b, rt_.stamp);
        l->act = {};
        l->bound = cores::MountState::Pending;
        chan_.send(std::move(l));
        o.sent = true;
    }
    return true;
}

void DiskRelay::poll_(proto::Link& link, const proto::DiskPollDecl& poll) noexcept {
    if (!proto::well_formed(poll))
        return;
    constexpr unsigned kByteMask = 0xFF;
    Ask::Command c{};
    {
        if (!link.command(hal::ChipSelect::Io, poll.opcode)) {
            rt_.wire_faults.add();
            return;
        }
        for (std::uint8_t i = 0; i < poll.beats; ++i) {
            const auto r = link.read();
            if (!r) {
                rt_.wire_faults.add();
                return;
            }
            c.state[i] = static_cast<std::byte>(*r & kByteMask);
        }
        link.close();
    }
    rt_.polls.add();
    const auto head = infra::load_native<std::uint8_t>(
        std::span(c.state).subspan(poll.head_byte).first<1>()
    );
    if ((head & poll.head_mask) == 0)
        return;
    Chan::Loan l = chan_.acquire();
    if (!l) {
        rt_.declines.add();
        return;
    }
    l->ask = ask_of(c, rt_.stamp);
    l->act = {};
    chan_.send(std::move(l));
    rt_.command_open = true;
    rt_.lent.add();
}

void DiskRelay::serve() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    while (Job job = chan_.take()) {
        infra::dispatch<infra::AllRouted>(job->ask, *this, job);
        job.complete();
    }
}

void DiskRelay::pump_on_caller() noexcept {

    if (sealed_) {
        sealed_pumps_.add();
        return;
    }
    const SeatScope stands_in_for_io{SeatTag::Io};
    serve();
}

void DiskRelay::begin_(const RelayStamp& h) noexcept {
    if (h.same_core(io_.served))
        return;
    io_.servant->release();
    io_.served = h;
}

void DiskRelay::on(const Ask::Bind& a, const RelayStamp& h, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    begin_(h);
    job->bound = io_.servant->bind(a.unit, a.path.view());
}

void DiskRelay::on(const Ask::Command& a, const RelayStamp& h, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    begin_(h);
    job->act = io_.servant->start(a.state, job->bytes);
}

void DiskRelay::on(const Ask::Resume&, const RelayStamp& h, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    begin_(h);
    job->act = io_.servant->resume(job->bytes);
}

void DiskRelay::on(const Ask::Release&, const RelayStamp& h, Job&) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    begin_(h);
    io_.servant->release();
}

void DiskRelay::misrouted(const Ask&, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    job->act = proto::DiskAct::none();
}

}
