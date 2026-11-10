// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/storage_service.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

#include "proto/storage_completion_dispatch.h"
#include "proto/storage_request_dispatch.h"

namespace mister::svc {

namespace {

proto::StorageStatus status_of(Errc e) noexcept {
    switch (e) {
        case Errc::not_found:
            return proto::StorageStatus::NotFound;
        case Errc::short_read:
            return proto::StorageStatus::ShortRead;
        case Errc::short_write:
            return proto::StorageStatus::ShortWrite;
        case Errc::bad_format:
            return proto::StorageStatus::BadFormat;
        case Errc::io:
        case Errc::os:
        case Errc::mount_failed:
        case Errc::timeout:
            return proto::StorageStatus::Io;
        case Errc::spi_timeout:
        case Errc::spi_nak:
        case Errc::fpga_not_ready:
        case Errc::bad_confstr:
        case Errc::bad_opcode:
        case Errc::slot_range:
        case Errc::negotiation:
        case Errc::mmap_failed:
        case Errc::uio_open:
        case Errc::dt_missing:
        case Errc::bridge_state:
        case Errc::core_load:
        case Errc::cancelled:
        case Errc::would_block:
        case Errc::unimplemented:
        case Errc::aperture_range:
        case Errc::stale:
        case Errc::busy:
        case Errc::seat_absent:
        case Errc::contract:
            return proto::StorageStatus::Refused;
    }

    return proto::StorageStatus::Refused;
}

}

namespace {

using Req = proto::StorageRequest;
using Cmp = proto::StorageCompletion;

Cmp::Head answer_head(const Req::Head& h, proto::StorageStatus st) noexcept {
    return Cmp::Head{h.seq, h.slot, st, {}};
}

enum class Stamp : std::uint8_t {
    Bound,
    Staged,
    Unbind,
};

struct Admit {
    Req::Head head{};
    proto::ArenaHalf half{};
    bool known = false;
    bool data = false;
    Stamp stamp = Stamp::Bound;
};

struct AdmitReader {
    using Result = Admit;

    Admit on(const Req::Read& a, const Req::Head& h) const noexcept {
        return {h, a.half, true, true, Stamp::Bound};
    }

    Admit on(const Req::Write& a, const Req::Head& h) const noexcept {
        return {h, a.half, true, true, Stamp::Bound};
    }

    Admit on(const Req::Create& a, const Req::Head& h) const noexcept {
        return {h, a.half, true, true, Stamp::Bound};
    }

    Admit on(const Req::Flush&, const Req::Head& h) const noexcept {
        return {h, {}, true, false, Stamp::Bound};
    }

    Admit on(const Req::Attach&, const Req::Head& h) const noexcept {
        return {h, {}, true, false, Stamp::Staged};
    }

    Admit on(const Req::Detach&, const Req::Head& h) const noexcept {
        return {h, {}, true, false, Stamp::Unbind};
    }

    Admit misrouted(const Req&) const noexcept { return {}; }
};

static_assert(proto::StorageRequestSink<AdmitReader>);

template <class T>
[[nodiscard]] constexpr std::span<T> clamped_extent(
    std::span<T> whole,
    std::uint32_t bytes
) noexcept {
    return whole.first(std::min<std::size_t>(whole.size(), static_cast<std::size_t>(bytes)));
}

}

static_assert(proto::StorageRequestSink<StorageService, StorageService::Work>);

infra::OptRef<IStorageBackend> StorageService::live_(
    proto::SlotIndex slot,
    const Work& w
) const noexcept {
    const auto served = io_.slots.find(slot);
    if (!served || !served->serves(w.gen))
        return {};
    return w.backend;
}

void StorageService::on(const Req::Read& a, const Req::Head& h, Work& w) noexcept {
    Cmp::Read c{.offset = a.offset, .half = a.half};
    proto::StorageStatus st = proto::StorageStatus::Ok;
    if (const auto live = live_(h.slot, w); !live) {
        st = proto::StorageStatus::Refused;
    } else {
        IStorageBackend& b = *live;
        b.note_tag(h.tag);

        const std::span<std::uint8_t> whole = w.data;
        const std::span<std::uint8_t> dst = clamped_extent(whole, a.bytes);
        auto n = b.read_at(a.offset, dst);

        if (n) {
            const std::size_t got = *n < dst.size() ? *n : dst.size();
            if (got < dst.size())
                std::memset(dst.data() + got, 0, dst.size() - got);
            c.bytes = static_cast<std::uint32_t>(got);
        } else {
            st = status_of(n.error().code);
        }
    }
    finish_(st, w, infra::make<Cmp>(c, answer_head(h, st)));
}

void StorageService::on(const Req::Write& a, const Req::Head& h, Work& w) noexcept {
    Cmp::Write c{.offset = a.offset, .half = a.half};
    proto::StorageStatus st = proto::StorageStatus::Ok;
    if (const auto live = live_(h.slot, w); !live) {
        st = proto::StorageStatus::Refused;
    } else {
        IStorageBackend& b = *live;
        b.note_tag(h.tag);
        const std::span<const std::uint8_t> whole = w.data;
        const std::span<const std::uint8_t> want = clamped_extent(whole, a.bytes);
        auto n = b.write_at(a.offset, want);
        if (!n) {
            st = status_of(n.error().code);
        } else {
            if (*n != want.size())
                st = proto::StorageStatus::ShortWrite;
            c.bytes = static_cast<std::uint32_t>(*n);
        }
    }
    finish_(st, w, infra::make<Cmp>(c, answer_head(h, st)));
}

void StorageService::on(const Req::Create& a, const Req::Head& h, Work& w) noexcept {
    Cmp::Create c{.half = a.half};
    proto::StorageStatus st = proto::StorageStatus::Ok;
    if (const auto live = live_(h.slot, w); !live) {
        st = proto::StorageStatus::Refused;
    } else {
        IStorageBackend& b = *live;
        b.note_tag(h.tag);
        const std::span<const std::uint8_t> whole = w.data;
        const std::span<const std::uint8_t> ask = clamped_extent(whole, a.bytes);
        auto e = b.create(ask);
        if (e) {
            c.size = e->v;
            c.bytes = static_cast<std::uint32_t>(ask.size());
        } else {
            st = status_of(e.error().code);
        }
    }
    finish_(st, w, infra::make<Cmp>(c, answer_head(h, st)));
}

void StorageService::on(const Req::Flush&, const Req::Head& h, Work& w) noexcept {
    proto::StorageStatus st = proto::StorageStatus::Ok;
    if (const auto live = live_(h.slot, w); !live) {
        st = proto::StorageStatus::Refused;
    } else {
        IStorageBackend& b = *live;
        b.note_tag(h.tag);
        if (auto e = b.flush(); !e)
            st = status_of(e.error().code);
    }
    finish_(st, w, infra::make<Cmp>(Cmp::Flush{}, answer_head(h, st)));
}

void StorageService::on(const Req::Attach& a, const Req::Head& h, Work& w) noexcept {
    Cmp::Attach c{};
    proto::StorageStatus st = proto::StorageStatus::Ok;
    const auto served = io_.slots.find(h.slot);
    if (!served || !w.backend) {
        st = proto::StorageStatus::Refused;
    } else {
        served->serve(w.gen);
        auto e = w.backend->open_extent(h.slot, w.order);
        if (e) {
            c.size = e->v;
        } else {
            st = status_of(e.error().code);
        }
        served->attached(w.backend, e ? std::optional{*e} : std::nullopt, a.growable);
    }
    finish_(st, w, infra::make<Cmp>(c, answer_head(h, st)));
}

void StorageService::on(const Req::Detach&, const Req::Head& h, Work& w) noexcept {
    proto::StorageStatus st = proto::StorageStatus::Ok;
    const auto served = io_.slots.find(h.slot);
    if (!served) {
        st = proto::StorageStatus::Refused;
    } else {

        served->serve(w.gen);

        if (w.backend) {
            if (auto e = w.backend->flush(); !e)
                st = status_of(e.error().code);
        }

        if (w.backend)
            w.backend->release(h.slot);
        served->detached();
    }
    finish_(st, w, infra::make<Cmp>(Cmp::Detach{}, answer_head(h, st)));
}

void StorageService::misrouted(const Req&, Work&) noexcept {
    request_misrouted_.add();
}

void StorageService::finish_(proto::StorageStatus st, Work& w, const Cmp& c) noexcept {
    if (st != proto::StorageStatus::Ok)
        errors_.add();
    w.answer = c;
    w.performed = true;
    completions_.add();
}

void StorageService::perform_(const Req& ask, Work& w) noexcept {
    infra::dispatch<proto::StorageRequestRoutes>(ask, *this, w);
}

void StorageService::serve() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(StorageService, A);
    if (Ctl::Job j = ctl_.take()) {
        Work w{{}, j->answer, j->backend, j->gen, j->order.view()};
        perform_(j->ask, w);
        if (w.performed) {
            j->answer = w.answer;
            j.complete();
        }
        return;
    }
    if (Data::Job j = data_.take()) {
        Work w{std::span<std::uint8_t>{j->data, kHalfBytes}, j->answer, j->backend, j->gen};
        perform_(j->ask, w);
        if (w.performed) {
            j->answer = w.answer;
            j.complete();
        }
    }
}

bool StorageService::idle() const noexcept {
    return data_.outbound() == 0 && ctl_.outbound() == 0;
}

StorageService::~StorageService() {

    const SeatScope as_rt{SeatTag::RT};
    for (auto& slot : rt_.slots)
        slot.drop_hands();
}

void StorageService::own_all_() noexcept {
    if (rt_.owned)
        return;
    rt_.owned = true;
    for (std::size_t n = 0; n < Data::capacity(); ++n) {
        Data::Loan l = data_.acquire();

        const auto slot = data_.index_of(l);
        if (!slot)
            break;
        const HandAt at = HandAt::of(DataHand::of(*slot));
        rt_.slots[at.slot].hand(at.half) = std::move(l);
    }
    for (std::size_t n = 0; n < Ctl::capacity(); ++n) {
        Ctl::Loan l = ctl_.acquire();
        const auto slot = ctl_.index_of(l);
        if (!slot)
            break;
        const proto::SlotIndex i = xthread::index_as<proto::SlotIndex>(*slot);
        rt_.slots[i].ctl() = std::move(l);
    }
}

bool StorageService::home_(proto::SlotIndex slot) const noexcept {
    if (!rt_.owned)
        return true;
    return rt_.slots[slot].home();
}

std::optional<proto::StorageSeq> StorageService::submit(const proto::StorageRequest& req) noexcept {
    AdmitReader reader{};
    const Admit a = infra::dispatch<proto::StorageRequestRoutes>(req, reader);

    if (!a.known
        || !rt_.slots.find(a.head.slot)
        || !a.half.exists()
        || a.head.seq == proto::StorageSeq::kRefused) {
        refusals_.add();
        return std::nullopt;
    }
    own_all_();
    Rt::Slot& bind = rt_.slots[a.head.slot];

    if (a.data) {
        Data::Loan& l = bind.hand(a.half);
        if (!l) {
            refusals_.add();
            return std::nullopt;
        }
        l->ask = req;
        l->backend = bind.bound();
        l->gen = bind.gen();
        data_.send(std::move(l));
    } else {
        Ctl::Loan& l = bind.ctl();
        if (!l) {
            refusals_.add();
            return std::nullopt;
        }
        l->ask = req;
        switch (a.stamp) {
            case Stamp::Staged:

                l->backend = l->stage;
                if (l->stage) {
                    bind.bind(l->stage);
                    l->stage.reset();
                }
                break;
            case Stamp::Unbind:

                l->backend = bind.bound();
                l->stage.reset();
                l->order.clear();
                bind.unbind();
                break;
            case Stamp::Bound:
                l->backend = bind.bound();
                break;
        }
        l->gen = bind.gen();
        ctl_.send(std::move(l));
    }
    submits_.add();
    const auto depth = static_cast<std::uint32_t>(data_.outbound() + ctl_.outbound());
    if (depth > depth_max_.get())
        depth_max_.set(depth);
    return a.head.seq;
}

std::optional<proto::StorageCompletion> StorageService::reap() noexcept {
    own_all_();

    for (std::size_t n = 0; n < Ctl::capacity() + Data::capacity(); ++n) {
        bool completed = false;
        Cmp c{};
        if (Ctl::Loan l = ctl_.reap()) {

            const proto::SlotIndex slot = xthread::index_as<proto::SlotIndex>(*ctl_.index_of(l));
            completed = l.completed();
            if (completed)
                c = l->answer;
            l->backend.reset();
            rt_.slots[slot].ctl() = std::move(l);
        } else if (Data::Loan d = data_.reap()) {
            const HandAt at = HandAt::of(DataHand::of(*data_.index_of(d)));
            completed = d.completed();
            if (completed)
                c = d->answer;
            d->backend.reset();
            rt_.slots[at.slot].hand(at.half) = std::move(d);
        } else {
            return std::nullopt;
        }
        if (!completed) {
            abandoned_.add(1);
            continue;
        }
        return c;
    }
    return std::nullopt;
}

std::span<const std::uint8_t> StorageService::half(
    proto::SlotIndex slot,
    proto::ArenaHalf which
) const noexcept {
    if (!rt_.slots.find(slot) || !which.exists() || !rt_.owned)
        return {};
    const Data::Loan& l = rt_.slots[slot].hand(which);
    if (!l)
        return {};
    return std::span<const std::uint8_t>{l->data, kHalfBytes};
}

std::span<std::uint8_t> StorageService::half_mut(
    proto::SlotIndex slot,
    proto::ArenaHalf which
) noexcept {
    if (!rt_.slots.find(slot) || !which.exists())
        return {};
    own_all_();
    const Data::Loan& l = rt_.slots[slot].hand(which);
    if (!l)
        return {};
    return std::span<std::uint8_t>{l->data, kHalfBytes};
}

bool StorageService::stage_backend(
    proto::SlotIndex slot,
    IStorageBackend& backend,
    std::span<const std::byte> order
) noexcept {

    if (!rt_.slots.find(slot) || !home_(slot) || order.size() > kStorageOrderBytes) {
        refusals_.add();
        return false;
    }
    own_all_();
    BlockCtl& hand = *rt_.slots[slot].ctl();
    infra::OptRef<IStorageBackend>& staged = hand.stage;

    if (staged && &*staged == &backend) {
        set_order_(hand, order);
        return true;
    }
    if (staged) {
        refusals_.add();
        return false;
    }
    staged = infra::OptRef<IStorageBackend>{backend};
    set_order_(hand, order);
    return true;
}

void StorageService::set_order_(BlockCtl& hand, std::span<const std::byte> order) noexcept {
    (void)hand.order.set(order);
}

bool StorageService::release_slot(proto::SlotIndex slot) noexcept {
    if (!rt_.slots.find(slot))
        return false;
    if (!home_(slot)) {
        refusals_.add();
        return false;
    }
    Rt::Slot& bind = rt_.slots[slot];
    if (rt_.owned) {
        bind.ctl()->stage.reset();
        bind.ctl()->order.clear();
    }
    bind.forget();
    return true;
}

bool StorageService::quiesced() const noexcept {
    const auto d = data_.census();
    const auto c = ctl_.census();
    return d.idle + d.held_by_owner == d.live && c.idle + c.held_by_owner == c.live;
}

ISaveFilesOnIo::Save StorageService::save_on_io(proto::SlotIndex slot) const noexcept {
    TASTY_SEAT_BODY_MEDIATOR(StorageService, A);
    const auto served = io_.slots.find(slot);
    if (!served)
        return {};
    return served->save();
}

StorageCounters StorageService::counters() const noexcept {
    StorageCounters c{};
    c.submits = submits_.get();
    c.completions = completions_.get();
    c.errors = errors_.get();
    c.refusals = refusals_.get();
    c.depth_max = depth_max_.get();
    c.misrouted = request_misrouted_.get();
    return c;
}

}
