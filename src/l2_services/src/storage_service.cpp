// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/storage_service.h"

#include <cstring>
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

}

static_assert(proto::StorageRequestSink<StorageService, StorageService::Work>);

infra::OptRef<IStorageBackend> StorageService::live_(
    proto::SlotIndex slot,
    const Work& w
) const noexcept {
    if (slot.v >= kSlots || w.gen != io_.served[slot.v])
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
        const std::span<std::uint8_t> dst = whole.first(
            a.bytes > whole.size() ? whole.size() : a.bytes
        );
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
        const std::uint32_t want = a.bytes > whole.size() ? static_cast<std::uint32_t>(whole.size())
                                                          : a.bytes;
        auto n = b.write_at(a.offset, whole.first(want));
        if (!n) {
            st = status_of(n.error().code);
        } else {
            if (*n != want)
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
        const std::uint32_t want = a.bytes > whole.size() ? static_cast<std::uint32_t>(whole.size())
                                                          : a.bytes;
        auto e = b.create(whole.first(want));
        if (e) {
            c.size = e->v;
            c.bytes = want;
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
    if (h.slot.v >= kSlots || !w.backend) {
        st = proto::StorageStatus::Refused;
    } else {
        io_.served[h.slot.v] = w.gen;
        auto e = w.backend->open_extent(w.order);
        if (e) {
            c.size = e->v;
        } else {
            st = status_of(e.error().code);
        }

        ISaveFilesOnIo::Save& v = io_.saves[h.slot.v];
        v = ISaveFilesOnIo::Save{
            .file = w.backend,
            .size = proto::FileSize{e ? e->v : 0},
            .exists = e.has_value(),
            .growable = a.growable,
            .gen = v.gen + 1
        };
    }
    finish_(st, w, infra::make<Cmp>(c, answer_head(h, st)));
}

void StorageService::on(const Req::Detach&, const Req::Head& h, Work& w) noexcept {
    proto::StorageStatus st = proto::StorageStatus::Ok;
    if (h.slot.v >= kSlots) {
        st = proto::StorageStatus::Refused;
    } else {

        io_.served[h.slot.v] = w.gen;

        if (w.backend) {
            if (auto e = w.backend->flush(); !e)
                st = status_of(e.error().code);
        }

        if (w.backend)
            w.backend->release();
        io_.saves[h.slot.v] = ISaveFilesOnIo::Save{.gen = io_.saves[h.slot.v].gen + 1};
    }
    finish_(st, w, infra::make<Cmp>(Cmp::Detach{}, answer_head(h, st)));
}

void StorageService::misrouted(const Req&, Work&) noexcept {
    request_misrouted_.fetch_add(1, std::memory_order_relaxed);
}

void StorageService::finish_(proto::StorageStatus st, Work& w, const Cmp& c) noexcept {
    if (st != proto::StorageStatus::Ok)
        errors_.fetch_add(1, std::memory_order_relaxed);
    w.answer = c;
    w.performed = true;
    completions_.fetch_add(1, std::memory_order_relaxed);
}

void StorageService::perform_(const Req& ask, Work& w) noexcept {
    infra::dispatch<proto::StorageRequestRoutes>(ask, *this, w);
}

void StorageService::serve() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(StorageService, A);
    if (Ctl::Job j = ctl_.take()) {
        Work w{
            {},
            j->answer,
            j->backend,
            j->gen,
            std::span<const std::byte>{j->order, j->order_len}
        };
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
    for (auto& slot : rt_.data_hands)
        for (auto& l : slot)
            l = Data::Loan{};
    for (auto& l : rt_.ctl_hands)
        l = Ctl::Loan{};
}

void StorageService::own_all_() noexcept {
    if (rt_.owned)
        return;
    rt_.owned = true;
    for (std::size_t n = 0; n < Data::capacity(); ++n) {
        Data::Loan l = data_.acquire();
        const std::uint8_t i = data_.index_of(l);
        if (i < kSlots * kHalves)
            rt_.data_hands[i / kHalves][i % kHalves] = std::move(l);
    }
    for (std::size_t n = 0; n < Ctl::capacity(); ++n) {
        Ctl::Loan l = ctl_.acquire();
        const std::uint8_t i = ctl_.index_of(l);
        if (i < kSlots)
            rt_.ctl_hands[i] = std::move(l);
    }
}

bool StorageService::home_(unsigned slot) const noexcept {
    if (!rt_.owned)
        return true;
    return rt_.data_hands[slot][0] && rt_.data_hands[slot][1] && rt_.ctl_hands[slot];
}

proto::StorageSeq StorageService::submit(const proto::StorageRequest& req) noexcept {
    AdmitReader reader{};
    const Admit a = infra::dispatch<proto::StorageRequestRoutes>(req, reader);

    if (!a.known || a.head.slot.v >= kSlots || a.half.v >= kHalves || a.head.seq.v == 0) {
        refusals_.fetch_add(1, std::memory_order_relaxed);
        return proto::StorageSeq{0};
    }
    own_all_();
    const unsigned s = a.head.slot.v;

    if (a.data) {
        Data::Loan& l = rt_.data_hands[s][a.half.v];
        if (!l) {
            refusals_.fetch_add(1, std::memory_order_relaxed);
            return proto::StorageSeq{0};
        }
        l->ask = req;
        l->backend = rt_.bound[s];
        l->gen = rt_.gen[s];
        data_.send(std::move(l));
    } else {
        Ctl::Loan& l = rt_.ctl_hands[s];
        if (!l) {
            refusals_.fetch_add(1, std::memory_order_relaxed);
            return proto::StorageSeq{0};
        }
        l->ask = req;
        switch (a.stamp) {
            case Stamp::Staged:

                l->backend = l->stage;
                if (l->stage) {
                    rt_.bound[s] = l->stage;
                    ++rt_.gen[s];
                    l->stage.reset();
                }
                break;
            case Stamp::Unbind:

                l->backend = rt_.bound[s];
                l->stage.reset();
                l->order_len = 0;
                rt_.bound[s].reset();
                ++rt_.gen[s];
                break;
            case Stamp::Bound:
                l->backend = rt_.bound[s];
                break;
        }
        l->gen = rt_.gen[s];
        ctl_.send(std::move(l));
    }
    submits_.fetch_add(1, std::memory_order_relaxed);
    const auto depth = static_cast<std::uint32_t>(data_.outbound() + ctl_.outbound());
    if (depth > depth_max_.load(std::memory_order_relaxed))
        depth_max_.store(depth, std::memory_order_relaxed);
    return a.head.seq;
}

std::optional<proto::StorageCompletion> StorageService::reap() noexcept {
    own_all_();

    for (std::size_t n = 0; n < Ctl::capacity() + Data::capacity(); ++n) {
        unsigned slot = 0;
        bool completed = false;
        Cmp c{};
        if (Ctl::Loan l = ctl_.reap()) {
            slot = ctl_.index_of(l);
            completed = l.completed();
            if (completed)
                c = l->answer;
            l->backend.reset();
            rt_.ctl_hands[slot] = std::move(l);
        } else if (Data::Loan d = data_.reap()) {
            const std::uint8_t i = data_.index_of(d);
            slot = i / kHalves;
            completed = d.completed();
            if (completed)
                c = d->answer;
            d->backend.reset();
            rt_.data_hands[slot][i % kHalves] = std::move(d);
        } else {
            return std::nullopt;
        }
        if (!completed) {
            abandoned_.fetch_add(1, std::memory_order_relaxed);
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
    if (slot.v >= kSlots || which.v >= kHalves || !rt_.owned)
        return {};
    const Data::Loan& l = rt_.data_hands[slot.v][which.v];
    if (!l)
        return {};
    return std::span<const std::uint8_t>{l->data, kHalfBytes};
}

std::span<std::uint8_t> StorageService::half_mut(
    proto::SlotIndex slot,
    proto::ArenaHalf which
) noexcept {
    if (slot.v >= kSlots || which.v >= kHalves)
        return {};
    own_all_();
    const Data::Loan& l = rt_.data_hands[slot.v][which.v];
    if (!l)
        return {};
    return std::span<std::uint8_t>{l->data, kHalfBytes};
}

bool StorageService::stage_backend(
    proto::SlotIndex slot,
    IStorageBackend& backend,
    std::span<const std::byte> order
) noexcept {

    if (slot.v >= kSlots || !home_(slot.v) || order.size() > kStorageOrderBytes) {
        refusals_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    own_all_();
    BlockCtl& hand = *rt_.ctl_hands[slot.v];
    infra::OptRef<IStorageBackend>& staged = hand.stage;

    if (staged && &*staged == &backend) {
        set_order_(hand, order);
        return true;
    }
    if (staged) {
        refusals_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    staged = infra::OptRef<IStorageBackend>{backend};
    set_order_(hand, order);
    return true;
}

void StorageService::set_order_(BlockCtl& hand, std::span<const std::byte> order) noexcept {
    hand.order_len = static_cast<std::uint16_t>(order.size());
    if (!order.empty())
        std::memcpy(hand.order, order.data(), order.size());
}

bool StorageService::release_slot(proto::SlotIndex slot) noexcept {
    if (slot.v >= kSlots)
        return false;
    if (!home_(slot.v)) {
        refusals_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (rt_.owned) {
        rt_.ctl_hands[slot.v]->stage.reset();
        rt_.ctl_hands[slot.v]->order_len = 0;
    }
    rt_.bound[slot.v].reset();
    return true;
}

bool StorageService::quiesced() const noexcept {
    const auto d = data_.census();
    const auto c = ctl_.census();
    return d.idle + d.held_by_owner == d.live && c.idle + c.held_by_owner == c.live;
}

ISaveFilesOnIo::Save StorageService::save_on_io(proto::SlotIndex slot) const noexcept {
    TASTY_SEAT_BODY_MEDIATOR(StorageService, A);
    if (slot.v >= kSlots)
        return {};
    return io_.saves[slot.v];
}

StorageCounters StorageService::counters() const noexcept {
    StorageCounters c{};
    c.submits = submits_.load(std::memory_order_relaxed);
    c.completions = completions_.load(std::memory_order_relaxed);
    c.errors = errors_.load(std::memory_order_relaxed);
    c.refusals = refusals_.load(std::memory_order_relaxed);
    c.depth_max = depth_max_.load(std::memory_order_relaxed);
    c.state = quiesced() ? 1u : 2u;
    c.misrouted = request_misrouted_.load(std::memory_order_relaxed);
    return c;
}

}
