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

}  // namespace

namespace {

using Req = proto::StorageRequest;
using Cmp = proto::StorageCompletion;

Cmp::Head answer_head(const Req::Head& h, proto::StorageStatus st) noexcept {
    return Cmp::Head{h.seq, h.slot, st, {}};
}

struct Admit {
    Req::Head head{};
    proto::ArenaHalf half{};
    bool known = false;
    bool data = false;
};
struct AdmitReader {
    using Result = Admit;
    Admit on(const Req::Read& a, const Req::Head& h) const noexcept {
        return {h, a.half, true, true};
    }
    Admit on(const Req::Write& a, const Req::Head& h) const noexcept {
        return {h, a.half, true, true};
    }
    Admit on(const Req::Create& a, const Req::Head& h) const noexcept {
        return {h, a.half, true, true};
    }
    Admit on(const Req::Flush&, const Req::Head& h) const noexcept { return {h, {}, true, false}; }
    Admit on(const Req::Attach&, const Req::Head& h) const noexcept { return {h, {}, true, false}; }
    Admit on(const Req::Detach&, const Req::Head& h) const noexcept { return {h, {}, true, false}; }
    Admit misrouted(const Req&) const noexcept { return {}; }
};
static_assert(proto::StorageRequestSink<AdmitReader>);

}  // namespace

static_assert(proto::StorageRequestSink<StorageService, StorageService::Work>);

IStorageBackend* StorageService::data_backend_(proto::SlotIndex slot) noexcept {
    return slot.v < kSlots ? backends_[slot.v] : nullptr;
}

void StorageService::on(const Req::Read& a, const Req::Head& h, Work& w) noexcept {
    Cmp::Read c{.offset = a.offset, .half = a.half};
    proto::StorageStatus st = proto::StorageStatus::Ok;
    if (IStorageBackend* b = data_backend_(h.slot); b == nullptr) {
        st = proto::StorageStatus::Refused;
    } else {
        b->note_tag(h.tag);

        const std::span<std::uint8_t> whole = w.data;
        const std::span<std::uint8_t> dst =
            whole.first(a.bytes > whole.size() ? whole.size() : a.bytes);
        auto n = b->read_at(a.offset, dst);

        if (n) {
            const std::size_t got = *n < dst.size() ? *n : dst.size();
            if (got < dst.size()) std::memset(dst.data() + got, 0, dst.size() - got);
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
    if (IStorageBackend* b = data_backend_(h.slot); b == nullptr) {
        st = proto::StorageStatus::Refused;
    } else {
        b->note_tag(h.tag);
        const std::span<const std::uint8_t> whole = w.data;
        const std::uint32_t want =
            a.bytes > whole.size() ? static_cast<std::uint32_t>(whole.size()) : a.bytes;
        auto n = b->write_at(a.offset, whole.first(want));
        if (!n) {
            st = status_of(n.error().code);
        } else {
            if (*n != want) st = proto::StorageStatus::ShortWrite;
            c.bytes = static_cast<std::uint32_t>(*n);
        }
    }
    finish_(st, w, infra::make<Cmp>(c, answer_head(h, st)));
}

void StorageService::on(const Req::Create& a, const Req::Head& h, Work& w) noexcept {
    Cmp::Create c{.half = a.half};
    proto::StorageStatus st = proto::StorageStatus::Ok;
    if (IStorageBackend* b = data_backend_(h.slot); b == nullptr) {
        st = proto::StorageStatus::Refused;
    } else {
        b->note_tag(h.tag);
        const std::span<const std::uint8_t> whole = w.data;
        const std::uint32_t want =
            a.bytes > whole.size() ? static_cast<std::uint32_t>(whole.size()) : a.bytes;
        auto e = b->create(whole.first(want));
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
    if (IStorageBackend* b = data_backend_(h.slot); b == nullptr) {
        st = proto::StorageStatus::Refused;
    } else {
        b->note_tag(h.tag);
        if (auto e = b->flush(); !e) st = status_of(e.error().code);
    }
    finish_(st, w, infra::make<Cmp>(Cmp::Flush{}, answer_head(h, st)));
}

void StorageService::on(const Req::Attach&, const Req::Head& h, Work& w) noexcept {
    Cmp::Attach c{};
    proto::StorageStatus st = proto::StorageStatus::Ok;
    const unsigned s = h.slot.v;
    if (s >= kSlots || attach_[s] == nullptr) {
        st = proto::StorageStatus::Refused;
    } else {
        backends_[s] = attach_[s];
        attach_[s] = nullptr;
        auto e = backends_[s]->open_extent();
        if (e) {
            c.size = e->v;
        } else {
            st = status_of(e.error().code);
        }
    }
    finish_(st, w, infra::make<Cmp>(c, answer_head(h, st)));
}

void StorageService::on(const Req::Detach&, const Req::Head& h, Work& w) noexcept {
    proto::StorageStatus st = proto::StorageStatus::Ok;
    if (const unsigned s = h.slot.v; s >= kSlots) {
        st = proto::StorageStatus::Refused;
    } else {

        if (backends_[s] != nullptr) backends_[s]->release();
        backends_[s] = nullptr;

        attach_[s] = nullptr;
    }
    finish_(st, w, infra::make<Cmp>(Cmp::Detach{}, answer_head(h, st)));
}

void StorageService::misrouted(const Req&, Work&) noexcept {
    request_misrouted_.fetch_add(1, std::memory_order_relaxed);
}

void StorageService::finish_(proto::StorageStatus st, Work& w, const Cmp& c) noexcept {
    if (st != proto::StorageStatus::Ok) errors_.fetch_add(1, std::memory_order_relaxed);
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
        Work w{{}, j->answer};
        perform_(j->ask, w);
        if (w.performed) j.complete();
        return;
    }
    if (Data::Job j = data_.take()) {
        Work w{std::span<std::uint8_t>{j->data, kHalfBytes}, j->answer};
        perform_(j->ask, w);
        if (w.performed) j.complete();
    }
}

bool StorageService::idle() const noexcept { return data_.outbound() == 0 && ctl_.outbound() == 0; }

StorageService::~StorageService() {

    const SeatScope as_rt{SeatTag::RT};
    for (auto& slot : data_hands_)
        for (auto& l : slot)
            l = Data::Loan{};
    for (auto& l : ctl_hands_)
        l = Ctl::Loan{};
}

void StorageService::own_all_() noexcept {
    if (owned_) return;
    owned_ = true;
    for (std::size_t n = 0; n < Data::capacity(); ++n) {
        Data::Loan l = data_.acquire();
        const std::uint8_t i = data_.index_of(l);
        if (i < kSlots * kHalves) data_hands_[i / kHalves][i % kHalves] = std::move(l);
    }
    for (std::size_t n = 0; n < Ctl::capacity(); ++n) {
        Ctl::Loan l = ctl_.acquire();
        const std::uint8_t i = ctl_.index_of(l);
        if (i < kSlots) ctl_hands_[i] = std::move(l);
    }
}

bool StorageService::home_(unsigned slot) const noexcept {
    if (!owned_) return true;
    return data_hands_[slot][0] && data_hands_[slot][1] && ctl_hands_[slot];
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
        Data::Loan& l = data_hands_[s][a.half.v];
        if (!l) {
            refusals_.fetch_add(1, std::memory_order_relaxed);
            return proto::StorageSeq{0};
        }
        l->ask = req;
        data_.send(std::move(l));
    } else {
        Ctl::Loan& l = ctl_hands_[s];
        if (!l) {
            refusals_.fetch_add(1, std::memory_order_relaxed);
            return proto::StorageSeq{0};
        }
        l->ask = req;
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
            if (completed) c = l->answer;
            ctl_hands_[slot] = std::move(l);
        } else if (Data::Loan d = data_.reap()) {
            const std::uint8_t i = data_.index_of(d);
            slot = i / kHalves;
            completed = d.completed();
            if (completed) c = d->answer;
            data_hands_[slot][i % kHalves] = std::move(d);
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

std::span<const std::uint8_t> StorageService::half(proto::SlotIndex slot,
                                                   proto::ArenaHalf which) const noexcept {
    if (slot.v >= kSlots || which.v >= kHalves || !owned_) return {};
    const Data::Loan& l = data_hands_[slot.v][which.v];
    if (!l) return {};
    return std::span<const std::uint8_t>{l->data, kHalfBytes};
}

std::span<std::uint8_t> StorageService::half_mut(proto::SlotIndex slot,
                                                 proto::ArenaHalf which) noexcept {
    if (slot.v >= kSlots || which.v >= kHalves) return {};
    own_all_();
    const Data::Loan& l = data_hands_[slot.v][which.v];
    if (!l) return {};
    return std::span<std::uint8_t>{l->data, kHalfBytes};
}

bool StorageService::stage_backend(proto::SlotIndex slot, IStorageBackend& backend) noexcept {

    if (slot.v >= kSlots || !home_(slot.v)) {
        refusals_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    if (attach_[slot.v] == &backend) return true;
    if (attach_[slot.v] != nullptr) {
        refusals_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    attach_[slot.v] = &backend;
    return true;
}

bool StorageService::release_slot(proto::SlotIndex slot) noexcept {
    if (slot.v >= kSlots) return false;
    if (!home_(slot.v)) {
        refusals_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    attach_[slot.v] = nullptr;
    backends_[slot.v] = nullptr;
    return true;
}

bool StorageService::quiesced() const noexcept {
    const auto d = data_.census();
    const auto c = ctl_.census();
    return d.idle + d.held_by_owner == d.live && c.idle + c.held_by_owner == c.live;
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

}  // namespace mister::svc
