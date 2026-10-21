// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/disk_relay.h"

#include <span>
#include <utility>

#include "hal/selected.h"
#include "hal/spi_transport.h"
#include "infra/message_sum.h"

namespace mister::app {

namespace {

using Ask = DiskSlot::Ask;

[[nodiscard]] Ask ask_of(const auto& alt, std::uint16_t gen, std::uint16_t epoch) noexcept {
    return infra::make<Ask>(alt, Ask::Head{.gen = gen, .epoch = epoch});
}

}

void DiskRelay::install(std::unique_ptr<cores::IDiskServant> servant) noexcept {
    io_.servant = std::move(servant);
}

void DiskRelay::service_rt(hal::ISpiTransport& link, const proto::DiskPollDecl& poll) noexcept {
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

void DiskRelay::order_bind(std::uint8_t unit, std::string_view path) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    if (unit >= kUnits)
        return;
    Order& o = rt_.orders[unit];
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

cores::MountState DiskRelay::bind_answer(std::uint8_t unit) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    if (unit >= kUnits)
        return cores::MountState::Failed;
    const Order& o = rt_.orders[unit];
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
    do
        ++rt_.epoch;
    while (rt_.epoch == 0);
    close_command_();
    rt_.abandoned.add();
}

void DiskRelay::abandon_command_on(std::uint8_t unit) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    if (rt_.open_unit != proto::kNoDiskUnit && rt_.open_unit != unit)
        return;
    abandon_command();
}

void DiskRelay::forget() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    do
        ++rt_.gen;
    while (rt_.gen == 0);
    close_command_();
    rt_.orders = {};

    while (Chan::Loan l = chan_.reap())
        rt_.stale.add();
    rt_.release_owed = true;
    (void)send_orders_();
}

void DiskRelay::close_command_() noexcept {
    rt_.command_open = false;
    rt_.open_unit = proto::kNoDiskUnit;
}

void DiskRelay::land_(Chan::Loan&& l, hal::ISpiTransport* link) noexcept {
    if (!l.completed()) {
        rt_.stale.add();
        const bool ours = l->ask.head.gen == rt_.gen && l->ask.head.epoch == rt_.epoch;
        if (ours
            && (infra::kind_of(l->ask) == Ask::Kind::Command
                || infra::kind_of(l->ask) == Ask::Kind::Resume)) {
            close_command_();
        }
        return;
    }
    infra::dispatch<infra::AllRouted>(l->ask, *this, l, link);
}

void DiskRelay::on(
    const Ask::Bind& a,
    const Ask::Head& h,
    Chan::Loan& l,
    hal::ISpiTransport*
) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    if (h.gen != rt_.gen || a.unit >= kUnits) {
        rt_.stale.add();
        return;
    }
    Order& o = rt_.orders[a.unit];
    if (a.seq != o.seq)
        return;
    o.answered = a.seq;
    o.answer = l->bound;
}

void DiskRelay::on(
    const Ask::Command&,
    const Ask::Head& h,
    Chan::Loan& l,
    hal::ISpiTransport* link
) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    act_(h, l, link);
}

void DiskRelay::on(
    const Ask::Resume&,
    const Ask::Head& h,
    Chan::Loan& l,
    hal::ISpiTransport* link
) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    act_(h, l, link);
}

void DiskRelay::on(
    const Ask::Release&,
    const Ask::Head&,
    Chan::Loan&,
    hal::ISpiTransport*
) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
}

void DiskRelay::misrouted(const Ask&, Chan::Loan&, hal::ISpiTransport*) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, A);
    rt_.stale.add();
}

void DiskRelay::act_(const Ask::Head& h, Chan::Loan& l, hal::ISpiTransport* link) noexcept {
    if (h.gen != rt_.gen || h.epoch != rt_.epoch) {
        rt_.stale.add();
        return;
    }
    const proto::DiskAct a = l->act;
    if (a.unit != proto::kNoDiskUnit)
        rt_.open_unit = a.unit;
    if (a.kind == proto::DiskAct::Kind::None) {
        close_command_();
        return;
    }
    if (link == nullptr || !write_(*link, a, *l)) {
        rt_.wire_faults.add();
        close_command_();
        return;
    }
    rt_.acts.add();
    if (a.last) {
        close_command_();
        return;
    }
    l->ask = ask_of(Ask::Resume{}, rt_.gen, rt_.epoch);
    l->act = {};
    chan_.send(std::move(l));
}

bool DiskRelay::write_(hal::ISpiTransport& link, const proto::DiskAct& a, DiskSlot& s) noexcept {

    if ((a.len & 1u) != 0u || a.len > s.bytes.size())
        return false;
    hal::Selected cs(link, hal::ChipSelect::Io);
    if (!link.transfer(hal::SpiWord{a.opcode}))
        return false;
    switch (a.kind) {
        case proto::DiskAct::Kind::Out:
            for (std::size_t i = 0; i < a.len; i += 2) {
                const auto lo = static_cast<std::uint16_t>(s.bytes[i]);
                const auto hi = static_cast<std::uint16_t>(s.bytes[i + 1]);
                if (!link.transfer(hal::SpiWord{static_cast<std::uint16_t>(lo | (hi << 8))}))
                    return false;
            }
            return true;
        case proto::DiskAct::Kind::In:
            for (std::size_t i = 0; i < a.len; i += 2) {
                const auto r = link.transfer(hal::SpiWord{0});
                if (!r)
                    return false;
                s.bytes[i] = static_cast<std::byte>(r->v & 0x00FFu);
                s.bytes[i + 1] = static_cast<std::byte>((r->v >> 8) & 0x00FFu);
            }
            return true;
        case proto::DiskAct::Kind::Signal:
            return !a.has_word || link.transfer(hal::SpiWord{a.word}).has_value();
        case proto::DiskAct::Kind::None:
            return true;
    }
    return false;
}

bool DiskRelay::send_orders_() noexcept {
    if (rt_.release_owed) {
        Chan::Loan l = chan_.acquire();
        if (!l)
            return false;
        l->ask = ask_of(Ask::Release{}, rt_.gen, rt_.epoch);
        l->act = {};
        chan_.send(std::move(l));
        rt_.release_owed = false;
    }
    for (std::uint8_t unit = 0; unit < kUnits; ++unit) {
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
        l->ask = ask_of(b, rt_.gen, rt_.epoch);
        l->act = {};
        l->bound = cores::MountState::Pending;
        chan_.send(std::move(l));
        o.sent = true;
    }
    return true;
}

void DiskRelay::poll_(hal::ISpiTransport& link, const proto::DiskPollDecl& poll) noexcept {
    if (!proto::well_formed(poll))
        return;
    Ask::Command c{};
    {
        hal::Selected cs(link, hal::ChipSelect::Io);
        if (!link.transfer(hal::SpiWord{poll.opcode})) {
            rt_.wire_faults.add();
            return;
        }
        for (std::uint8_t i = 0; i < poll.beats; ++i) {
            const auto r = link.transfer(hal::SpiWord{0});
            if (!r) {
                rt_.wire_faults.add();
                return;
            }
            c.state[i] = static_cast<std::byte>(r->v & 0x00FFu);
        }
    }
    rt_.polls.add();
    if ((static_cast<std::uint8_t>(c.state[poll.head_byte]) & poll.head_mask) == 0)
        return;
    Chan::Loan l = chan_.acquire();
    if (!l) {
        rt_.declines.add();
        return;
    }
    l->ask = ask_of(c, rt_.gen, rt_.epoch);
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

void DiskRelay::begin_(const Ask::Head& h) noexcept {
    if (h.gen == io_.served_gen)
        return;
    if (io_.servant)
        io_.servant->release();
    io_.served_gen = h.gen;
}

void DiskRelay::on(const Ask::Bind& a, const Ask::Head& h, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    begin_(h);
    job->bound = io_.servant ? io_.servant->bind(a.unit, a.path.view()) : cores::MountState::Failed;
}

void DiskRelay::on(const Ask::Command& a, const Ask::Head& h, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    begin_(h);
    job->act = io_.servant ? io_.servant->start(a.state, job->bytes) : proto::DiskAct{.last = true};
}

void DiskRelay::on(const Ask::Resume&, const Ask::Head& h, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    begin_(h);
    job->act = io_.servant ? io_.servant->resume(job->bytes) : proto::DiskAct{.last = true};
}

void DiskRelay::on(const Ask::Release&, const Ask::Head& h, Job&) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    begin_(h);
    if (io_.servant)
        io_.servant->release();
}

void DiskRelay::misrouted(const Ask&, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelay, B);
    job->act = proto::DiskAct{.last = true};
}

}
