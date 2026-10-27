// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/block_slots.h"
#include "proto/save_slot_rule.h"
#include "proto/disc_announce.h"
#include "proto/spi_frame.h"
#include "hal/selected.h"
#include "proto/storage_completion_dispatch.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace mister::proto {

namespace {

constexpr std::uint16_t kSectorRd = 0x17;
constexpr std::uint16_t kSectorWr = 0x18;
constexpr std::uint16_t kSetSdStat = DiscAnnounce::kSetSdStat;
constexpr std::uint16_t kSetSdInfo = DiscAnnounce::kSetSdInfo;

constexpr std::array<std::uint8_t, kBlockStagingBytes> kZeroPage{};
constexpr std::array<std::uint8_t, kBlockStagingBytes> kFfPage = [] {
    std::array<std::uint8_t, kBlockStagingBytes> a{};
    a.fill(0xFF);
    return a;
}();

[[nodiscard]] Ex<void> write32_byte(ILinkFrame& frame, std::uint32_t v) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        if (auto r = frame.word(static_cast<std::uint16_t>((v >> shift) & 0xFFu)); !r)
            return r;
    }
    return {};
}

struct SlotOf {
    using Result = SlotIndex;

    template <class A>
    SlotIndex on(const A&, const StorageCompletion::Head& h) const noexcept {
        return h.slot;
    }

    SlotIndex misrouted(const StorageCompletion&) const noexcept { return SlotIndex{kBlockSlots}; }
};

}

BlockSlots::BlockSlots() noexcept : BlockSlots(Wiring{}) {}

BlockSlots::BlockSlots(Wiring w) noexcept : chan_(w.channel), clock_(w.clock), source_(w.source) {}

Ex<void> BlockSlots::announce(
    hal::ISpiTransport& link,
    SlotIndex slot,
    FileSize size_bytes,
    bool writable
) {
    if (auto r = block_poll_.send_config(link); !r)
        return r;

    SpiFrame frame{link};
    if (auto r = frame.begin(FrameSel::Uio, kSetSdInfo); !r)
        return r;
    const auto lo = static_cast<std::uint32_t>(size_bytes.v & 0xFFFFFFFFu);
    const auto hi = static_cast<std::uint32_t>(size_bytes.v >> 32);
    if (link.io_version() != 0) {
        if (auto r = DiscAnnounce::write32_word(frame, lo); !r)
            return r;
        if (auto r = DiscAnnounce::write32_word(frame, hi); !r)
            return r;
    } else {
        if (auto r = write32_byte(frame, lo); !r)
            return r;
        if (auto r = write32_byte(frame, hi); !r)
            return r;
    }
    if (auto r = frame.end(); !r)
        return r;

    if (slot.v > kMaxAnnounceableSlot) {
        ++diag_.unencodable_announce;
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), slot.v});
    }
    const auto parm = static_cast<std::uint16_t>((1u << slot.v) | (writable ? 0u : 0x80u));
    if (auto r = frame.begin(FrameSel::Uio, kSetSdStat); !r)
        return r;
    if (auto r = frame.word(parm); !r)
        return r;
    return frame.end();
}

Ex<void> BlockSlots::mount(
    hal::ISpiTransport& link,
    SlotIndex slot,
    FileSize size_bytes,
    PathId path_id
) {
    if (slot.v >= kBlockSlots) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot.v});
    }

    const infra::OptRef<IImageSource> src = source_for(slot);
    const SlotAttributes attr = src ? src->attributes(slot) : SlotAttributes{};

    Slot& s = records_[slot.v].slot;
    s.state = Slot::State::Mounted;
    s.block_size = 512;

    s.size_bytes = attr.deferred_create ? FileSize{0}
        : attr.channel_bytes != 0       ? FileSize{attr.channel_bytes}
                                        : size_bytes;
    s.file_bytes = attr.deferred_create ? FileSize{0} : size_bytes;
    s.path_id = path_id;
    s.writable = attr.writable;
    s.growable = attr.growable;
    s.deferred_create = attr.deferred_create;

    records_[slot.v].drop_window();

    return announce(link, slot, size_bytes, attr.writable);
}

Ex<void> BlockSlots::unmount(hal::ISpiTransport& link, SlotIndex slot) {
    if (slot.v >= kBlockSlots) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot.v});
    }

    records_[slot.v].slot = Slot{};
    records_[slot.v].drop_window();
    return announce(link, slot, FileSize{0}, false);
}

Ex<void> BlockSlots::install_disc(SlotIndex slot, FileSize size_bytes) {
    if (slot.v >= kBlockSlots) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot.v});
    }
    if (size_bytes.v == 0) {
        records_[slot.v].slot = Slot{};
    } else {
        const infra::OptRef<IImageSource> src = source_for(slot);
        const SlotAttributes attr = src ? src->attributes(slot) : SlotAttributes{};
        Slot& s = records_[slot.v].slot;
        s.state = Slot::State::Mounted;
        s.block_size = 512;
        s.size_bytes = size_bytes;
        s.file_bytes = size_bytes;
        s.path_id = PathId{};
        s.writable = attr.writable;
        s.growable = attr.growable;
        s.deferred_create = attr.deferred_create;
    }

    records_[slot.v].drop_window();
    return {};
}

const BlockSlots::Slot& BlockSlots::slot(SlotIndex i) const {
    if (i.v >= kBlockSlots) {
        fatal(Error{Errc::slot_range, ERR_SITE(), i.v}, "block slot index");
    }
    return records_[i.v].slot;
}

void BlockSlots::invalidate(SlotIndex i) {
    if (i.v >= kBlockSlots) {
        fatal(Error{Errc::slot_range, ERR_SITE(), i.v}, "block slot index");
    }
    records_[i.v].drop_window();
}

std::int64_t BlockSlots::now_ns() const noexcept {
    return clock_ ? clock_->now().count() : 0;
}

void BlockSlots::note_answer_wait_(std::int64_t waited_ns) noexcept {
    if (waited_ns <= kLateAnswerNs)
        return;
    ++diag_.late_answers;
    const std::int64_t us = std::min<std::int64_t>(waited_ns / 1000, 0xFFFF'FFFF);
    late_max_us_ = std::max(late_max_us_, static_cast<std::uint32_t>(us));
}

BlockSlots::Refill BlockSlots::refill_resident(
    SlotIndex slot,
    Lba base,
    std::uint32_t block_size,
    IResidentImageSource& src,
    bool answer_owed
) {
    Window& w = records_[slot.v].window;

    const ArenaHalf served = w.half;
    w = Window{};
    w.half = served;
    const Slot& s = records_[slot.v].slot;
    if (s.state != Slot::State::Mounted || s.size_bytes.v == 0) {
        return Refill::Miss;
    }

    if (!chan_)
        return Refill::Miss;
    const ArenaHalf half = records_[slot.v].fill_half();
    const std::span<std::uint8_t> dst = chan_->half_mut(slot, half);
    if (dst.empty())
        return Refill::Miss;

    const std::uint64_t offset = static_cast<std::uint64_t>(base.v) * block_size;
    auto n = src.read_at(slot, offset, dst);
    if (!n) {

        if (n.error().code == Errc::would_block && answer_owed && clock_.has_value()) {

            const std::int64_t now = now_ns();
            if (records_[slot.v].resident_due_ns == 0)
                records_[slot.v].resident_due_ns = now + kAnswerDeadlineNs;
            if (now < records_[slot.v].resident_due_ns)
                return Refill::Defer;
            ++diag_.answer_expiries;
            note_answer_wait_(kAnswerDeadlineNs);
        }
        records_[slot.v].resident_due_ns = 0;
        return Refill::Miss;
    }
    if (records_[slot.v].resident_due_ns != 0)
        note_answer_wait_(now_ns() - (records_[slot.v].resident_due_ns - kAnswerDeadlineNs));
    records_[slot.v].resident_due_ns = 0;
    auto got = static_cast<std::uint32_t>(*n);
    if (got > kBlockStagingBytes)
        got = kBlockStagingBytes;

    if (got < kBlockStagingBytes) {
        std::memset(dst.data() + got, 0, kBlockStagingBytes - got);
    }
    if (got == 0)
        return Refill::Miss;

    w.base = base;
    w.valid_bytes = got;
    w.block_size = block_size;
    w.half = half;
    return Refill::Hit;
}

bool BlockSlots::submit_fill(
    SlotIndex slot,
    Lba base,
    std::uint32_t block_size,
    bool answer_owed
) noexcept {
    Window& w = records_[slot.v].window;
    const ArenaHalf served = w.half;
    w = Window{};
    w.half = served;
    const Slot& s = records_[slot.v].slot;
    if (s.state != Slot::State::Mounted || s.size_bytes.v == 0)
        return false;
    if (!storage_live_ || !chan_ || !source_for(slot))
        return false;
    SlotGate& p = records_[slot.v].gate;
    if (p.state != SlotGate::State::Idle)
        return false;

    const ArenaHalf half = records_[slot.v].fill_half();
    if (half.v >= kNoHalf)
        return false;

    const StorageSeq seq = chan_->submit(
        infra::make<StorageRequest>(
            StorageRequest::Read{
                .offset = static_cast<std::uint64_t>(base.v) * block_size,
                .bytes = kBlockStagingBytes,
                .half = half
            },
            request_head_(slot)
        )
    );
    if (seq.v == 0)
        return false;
    if (++next_seq_ == 0)
        next_seq_ = 1;
    records_[slot.v].half_busy |= static_cast<std::uint8_t>(1u << half.v);

    p = SlotGate{};
    p.state = SlotGate::State::Fetching;
    p.seq = seq;
    p.epoch = records_[slot.v].epoch;
    p.base = base;
    p.block_size = block_size;
    p.answer_owed = answer_owed;
    p.due_ns = now_ns() + (answer_owed ? kAnswerDeadlineNs : kPrefetchDeadlineNs);
    return true;
}

StorageRequest::Head BlockSlots::request_head_(SlotIndex slot) const noexcept {
    StorageRequest::Head h{StorageSeq{next_seq_}, slot, 0, {}};
    if (const auto src = source_for(slot))
        h.tag = src->request_tag(slot);
    return h;
}

bool BlockSlots::submit_create(SlotIndex slot, ArenaHalf half, std::uint32_t len) noexcept {
    if (!storage_live_ || !chan_)
        return false;
    if (half.v >= kNoHalf)
        return false;
    if (records_[slot.v].gate.state != SlotGate::State::Idle)
        return false;
    const StorageSeq seq = chan_->submit(
        infra::make<StorageRequest>(
            StorageRequest::Create{.bytes = len, .half = half},
            request_head_(slot)
        )
    );
    if (seq.v == 0)
        return false;
    gate_commit_(slot, seq, half, 0, len);
    return true;
}

bool BlockSlots::submit_write(
    SlotIndex slot,
    ArenaHalf half,
    std::uint64_t offset,
    std::uint32_t len
) noexcept {
    if (!storage_live_ || !chan_)
        return false;
    if (half.v >= kNoHalf)
        return false;
    if (records_[slot.v].gate.state != SlotGate::State::Idle)
        return false;
    const StorageSeq seq = chan_->submit(
        infra::make<StorageRequest>(
            StorageRequest::Write{.offset = offset, .bytes = len, .half = half},
            request_head_(slot)
        )
    );
    if (seq.v == 0)
        return false;
    gate_commit_(slot, seq, half, offset, len);
    return true;
}

void BlockSlots::gate_commit_(
    SlotIndex slot,
    StorageSeq seq,
    ArenaHalf half,
    std::uint64_t offset,
    std::uint32_t len
) noexcept {
    if (++next_seq_ == 0)
        next_seq_ = 1;
    records_[slot.v].half_busy |= static_cast<std::uint8_t>(1u << half.v);
    SlotGate& p = records_[slot.v].gate;
    p = SlotGate{};
    p.state = SlotGate::State::Committing;
    p.seq = seq;
    p.epoch = records_[slot.v].epoch;
    p.offset = offset;
    p.len = len;
    p.due_ns = now_ns() + kAnswerDeadlineNs;
}

bool BlockSlots::submit_attach(SlotIndex slot) noexcept {
    if (slot.v >= kBlockSlots || !storage_live_ || !chan_)
        return false;
    if (records_[slot.v].gate.state != SlotGate::State::Idle)
        return false;

    const infra::OptRef<IImageSource> src = source_for(slot);
    const bool growable = src && src->attributes(slot).growable;
    return submit_lifecycle_(
        slot,
        infra::make<StorageRequest>(
            StorageRequest::Attach{.growable = growable},
            request_head_(slot)
        )
    );
}

bool BlockSlots::submit_detach(SlotIndex slot) noexcept {
    if (slot.v >= kBlockSlots || !storage_live_ || !chan_)
        return false;
    if (records_[slot.v].gate.state != SlotGate::State::Idle)
        return false;
    return submit_lifecycle_(
        slot,
        infra::make<StorageRequest>(StorageRequest::Detach{}, request_head_(slot))
    );
}

bool BlockSlots::submit_lifecycle_(SlotIndex slot, const StorageRequest& r) noexcept {
    SlotGate& p = records_[slot.v].gate;
    const StorageSeq seq = chan_->submit(r);
    if (seq.v == 0)
        return false;
    if (++next_seq_ == 0)
        next_seq_ = 1;

    p = SlotGate{};
    p.state = SlotGate::State::Staging;
    p.seq = seq;
    p.epoch = records_[slot.v].epoch;
    p.due_ns = now_ns() + kStagingDeadlineNs;
    return true;
}

static_assert(StorageCompletionSink<BlockSlots>);

void BlockSlots::apply_completion(SlotIndex slot, const StorageCompletion& c) noexcept {
    infra::dispatch<StorageCompletionRoutes>(c, *this, slot);
}

bool BlockSlots::stale_(SlotIndex slot, const StorageCompletion::Head& h) noexcept {
    const SlotGate& p = records_[slot.v].gate;
    if (p.state == SlotGate::State::Idle || p.seq != h.seq) {
        ++diag_.stale_completions;
        return true;
    }
    return false;
}

void BlockSlots::on(
    const StorageCompletion::Read& a,
    const StorageCompletion::Head& h,
    SlotIndex slot
) noexcept {
    records_[slot.v].half_busy &= static_cast<std::uint8_t>(~(1u << a.half.v));
    if (stale_(slot, h))
        return;
    const SlotGate& p = records_[slot.v].gate;
    bool blank_next = false;
    if (p.epoch != records_[slot.v].epoch) {

        ++diag_.stale_completions;
    } else if (h.status == StorageStatus::Ok && a.bytes != 0) {
        Window& w = records_[slot.v].window;
        w.base = p.base;
        w.valid_bytes = a.bytes > kBlockStagingBytes ? kBlockStagingBytes : a.bytes;
        w.block_size = p.block_size;
        w.half = a.half;
    } else if (p.answer_owed) {
        blank_next = true;
    }
    records_[slot.v].finish_gate(blank_next);
}

void BlockSlots::on(
    const StorageCompletion::Write& a,
    const StorageCompletion::Head& h,
    SlotIndex slot
) noexcept {
    records_[slot.v].half_busy &= static_cast<std::uint8_t>(~(1u << a.half.v));
    if (stale_(slot, h))
        return;
    const SlotGate& p = records_[slot.v].gate;
    Slot& s = records_[slot.v].slot;
    if (p.epoch != records_[slot.v].epoch) {

        ++diag_.stale_completions;
    } else if (h.status == StorageStatus::Ok && a.bytes == p.len) {

        if (const std::uint64_t end = p.offset + p.len; end > s.size_bytes.v) {
            s.size_bytes = FileSize{end};
        }
    } else {
        ++diag_.write_failures;
    }
    records_[slot.v].finish_gate(false);
}

void BlockSlots::on(
    const StorageCompletion::Create& a,
    const StorageCompletion::Head& h,
    SlotIndex slot
) noexcept {
    records_[slot.v].half_busy &= static_cast<std::uint8_t>(~(1u << a.half.v));
    if (stale_(slot, h))
        return;
    Slot& s = records_[slot.v].slot;
    if (records_[slot.v].gate.epoch != records_[slot.v].epoch) {
        ++diag_.stale_completions;
    } else if (h.status == StorageStatus::Ok) {
        s.size_bytes = FileSize{a.size};
        s.file_bytes = FileSize{a.size};
        s.deferred_create = false;
    } else {
        ++diag_.write_failures;
    }
    records_[slot.v].finish_gate(false);
}

void BlockSlots::on(
    const StorageCompletion::Flush&,
    const StorageCompletion::Head& h,
    SlotIndex slot
) noexcept {
    if (stale_(slot, h))
        return;
    ++diag_.stale_completions;
}

void BlockSlots::on(
    const StorageCompletion::Attach& a,
    const StorageCompletion::Head& h,
    SlotIndex slot
) noexcept {
    if (const auto src = source_for(slot)) {
        const bool ok = h.status == StorageStatus::Ok;
        src->note_attached(slot, ok, FileSize{ok ? a.size : 0});
    }
    if (stale_(slot, h))
        return;
    records_[slot.v].finish_gate(false);
}

void BlockSlots::on(
    const StorageCompletion::Detach&,
    const StorageCompletion::Head& h,
    SlotIndex slot
) noexcept {
    if (const auto src = source_for(slot))
        src->note_detached(slot);
    if (stale_(slot, h))
        return;
    records_[slot.v].finish_gate(false);
}

void BlockSlots::misrouted(const StorageCompletion&, SlotIndex) noexcept {
    TASTY_SEAT_BODY(BlockSlots);
    ++diag_.stale_completions;
}

void BlockSlots::expire(SlotIndex slot) noexcept {
    SlotGate& p = records_[slot.v].gate;
    bool blank_next = false;
    switch (p.state) {
        case SlotGate::State::Fetching:
            if (p.answer_owed) {
                ++diag_.answer_expiries;
                blank_next = true;
            } else {
                ++diag_.prefetch_expiries;
            }
            break;
        case SlotGate::State::Committing:
            ++diag_.write_failures;
            break;
        case SlotGate::State::Staging:

            ++diag_.staging_expiries;
            break;
        case SlotGate::State::Idle:
            break;
    }
    p = SlotGate{};
    p.blank_next = blank_next;
}

unsigned BlockSlots::install_completions() noexcept {
    if (!chan_)
        return 0;
    unsigned n = 0;
    const std::int64_t now = now_ns();
    SlotOf route{};

    while (const auto c = chan_->reap()) {
        ++n;
        if (const SlotIndex slot = infra::dispatch<StorageCompletionRoutes>(*c, route);
            slot.v < kBlockSlots) {
            apply_completion(slot, *c);
        } else {
            ++diag_.stale_completions;
        }
    }
    for (unsigned i = 0; i < kBlockSlots; ++i) {
        const SlotIndex slot{static_cast<std::uint8_t>(i)};
        const SlotGate& p = records_[i].gate;
        if (p.state != SlotGate::State::Idle && clock_.has_value() && now >= p.due_ns) {
            expire(slot);
        }
    }
    return n;
}

BlockSlots::Blank BlockSlots::no_half_answer(SlotIndex slot) noexcept {
    std::int64_t& due = records_[slot.v].no_half_due_ns;
    if (!clock_) {
        ++diag_.no_half_substitutes;
        due = 0;
        return Blank::Static;
    }
    const std::int64_t now = now_ns();
    if (due == 0)
        due = now + kAnswerDeadlineNs;
    if (now < due)
        return Blank::Deferred;
    due = 0;
    ++diag_.no_half_substitutes;
    return Blank::Static;
}

BlockSlots::Blank BlockSlots::substitute_blank(SlotIndex slot, Lba lba) {

    if (chan_ && records_[slot.v].half_held(records_[slot.v].window.half)) {
        const ArenaHalf free_half = records_[slot.v].fill_half();

        if (free_half.v >= kNoHalf)
            return no_half_answer(slot);
        records_[slot.v].window.half = free_half;
    }
    records_[slot.v].no_half_due_ns = 0;
    ++diag_.blank_filled;
    if (!chan_)
        return Blank::NoBuffer;
    std::span<std::uint8_t> window = chan_->half_mut(slot, records_[slot.v].window.half);
    if (window.empty())
        return Blank::NoBuffer;
    if (records_[slot.v].slot.deferred_create) {
        const infra::OptRef<IImageSource> src = source_for(slot);
        if (src && src->fill_blank(slot, lba, window))
            return Blank::Filled;
        std::memset(window.data(), 0xFF, window.size());
        return Blank::Filled;
    }
    std::memset(window.data(), 0x00, window.size());
    return Blank::Filled;
}

[[nodiscard]] Ex<void> BlockSlots::emit_read_data_phase_(
    hal::ISpiTransport& link,
    SlotIndex slot,
    std::span<const std::uint8_t> serve,
    std::uint32_t offset,
    std::uint32_t nbytes,
    std::uint16_t ack,
    std::uint32_t blks,
    bool count_blocks
) {
    hal::Selected cs(link, hal::ChipSelect::Io);
    const auto cmd = static_cast<std::uint16_t>(kSectorRd | ack);
    if (auto r = link.transfer(hal::SpiWord{cmd}); !r) {
        return std::unexpected(r.error());
    }
    if (serve.size() < static_cast<std::size_t>(offset) + nbytes) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), slot.v});
    }
    if (auto r = link.block_write(serve.subspan(offset, nbytes)); !r) {
        return std::unexpected(r.error());
    }

    if (count_blocks)
        diag_.blocks_served += blks;
    return {};
}

Ex<bool> BlockSlots::answer_read(
    hal::ISpiTransport& link,
    SlotIndex slot,
    Lba lba,
    std::uint32_t nbytes,
    std::uint16_t ack
) {
    if (slot.v >= kBlockSlots || nbytes == 0)
        return false;

    if (records_[slot.v].gate.state != SlotGate::State::Idle)
        return false;
    const Window& w = records_[slot.v].window;

    const std::uint32_t block_size = w.block_size;
    if (block_size == 0 || nbytes % block_size != 0)
        return false;
    const std::uint32_t blks = nbytes / block_size;
    const std::uint32_t window_blocks = kBlockStagingBytes / block_size;
    const bool hit = w.valid_bytes != 0
        && lba >= w.base
        && (static_cast<std::uint64_t>(lba.v) + blks - w.base.v) <= window_blocks;
    if (!hit)
        return false;
    const std::uint32_t offset = (lba.v - w.base.v) * block_size;
    const std::span<const std::uint8_t> serve = chan_ ? chan_->half(slot, w.half)
                                                      : std::span<const std::uint8_t>{};
    if (auto r = emit_read_data_phase_(link, slot, serve, offset, nbytes, ack, blks, true); !r) {
        return std::unexpected(r.error());
    }
    return true;
}

Ex<BlockSlots::Pass> BlockSlots::serve_read(
    hal::ISpiTransport& link,
    const SdRequest& req,
    std::uint32_t block_size,
    std::uint32_t window_blocks,
    std::uint16_t ack,
    infra::OptRef<IResidentImageSource> res
) {
    const std::uint32_t blks = req.block_count.v;
    const std::uint32_t sz = block_size * blks;
    Window& w = records_[req.slot.v].window;

    const bool hit = w.valid_bytes != 0
        && w.block_size == block_size
        && req.lba >= w.base
        && (static_cast<std::uint64_t>(req.lba.v) + blks - w.base.v) <= window_blocks;

    std::uint32_t offset = 0;
    bool cached = hit;
    Blank blank = Blank::Filled;
    if (hit) {
        offset = (req.lba.v - w.base.v) * block_size;
    } else if (res) {
        const Refill r = refill_resident(req.slot, req.lba, block_size, *res, true);
        cached = r == Refill::Hit;
        if (r == Refill::Defer)
            blank = Blank::Deferred;
        else if (r == Refill::Miss)
            blank = substitute_blank(req.slot, req.lba);
    } else if (records_[req.slot.v].gate.blank_next) {

        blank = substitute_blank(req.slot, req.lba);
        if (blank != Blank::Deferred)
            records_[req.slot.v].gate.blank_next = false;
    } else if (submit_fill(req.slot, req.lba, block_size, true)) {

        ++diag_.deferred_passes;
        return Pass::Deferred;
    } else {
        blank = substitute_blank(req.slot, req.lba);
    }
    if (blank == Blank::Deferred) {
        ++diag_.deferred_passes;
        return Pass::Deferred;
    }

    const std::span<const std::uint8_t> serve = blank == Blank::Static
        ? std::span<
              const std::uint8_t>{records_[req.slot.v].slot.deferred_create ? kFfPage : kZeroPage}
        : chan_ ? chan_->half(req.slot, records_[req.slot.v].window.half)
                : std::span<const std::uint8_t>{};
    if (auto r = emit_read_data_phase_(link, req.slot, serve, offset, sz, ack, blks, cached); !r) {
        return std::unexpected(r.error());
    }

    if (records_[req.slot.v].slot.deferred_create) {
        records_[req.slot.v].window = Window{};
        return Pass::Answered;
    }
    if (!cached)
        return Pass::Answered;
    const std::uint64_t consumed = static_cast<std::uint64_t>(req.lba.v)
        + blks
        - records_[req.slot.v].window.base.v;
    if (consumed == window_blocks) {
        const Lba next{req.lba.v + blks};
        if (res) {

            if (refill_resident(req.slot, next, block_size, *res, false) != Refill::Hit) {
                if (chan_) {
                    const auto win = chan_->half_mut(req.slot, records_[req.slot.v].window.half);
                    if (!win.empty())
                        std::memset(win.data(), 0, win.size());
                }
            }
        } else {

            (void)submit_fill(req.slot, next, block_size, false);
        }
    }
    return Pass::Answered;
}

Ex<BlockSlots::Pass> BlockSlots::serve_write(
    hal::ISpiTransport& link,
    const SdRequest& req,
    std::uint32_t block_size,
    std::uint16_t ack,
    infra::OptRef<IResidentImageSource> res
) {
    const std::uint32_t sz = block_size * req.block_count.v;

    if (!chan_) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), req.slot.v});
    }
    const ArenaHalf drain_half = records_[req.slot.v].fill_half();
    if (drain_half.v >= kNoHalf) {
        ++diag_.deferred_passes;
        return Pass::Deferred;
    }

    records_[req.slot.v].window = Window{};
    {
        hal::Selected cs(link, hal::ChipSelect::Io);
        const auto cmd = static_cast<std::uint16_t>(kSectorWr | ack);
        if (auto r = link.transfer(hal::SpiWord{cmd}); !r) {
            return std::unexpected(r.error());
        }
        const std::span<std::uint8_t> drain = chan_->half_mut(req.slot, drain_half);
        if (drain.size() < sz) {
            return std::unexpected(Error{Errc::io, ERR_SITE(), req.slot.v});
        }
        if (auto r = link.block_read(drain.first(sz)); !r) {
            return std::unexpected(r.error());
        }
    }

    Slot& s = records_[req.slot.v].slot;
    const infra::OptRef<IImageSource> src = source_for(req.slot);
    if (!src || s.state != Slot::State::Mounted) {
        ++diag_.write_failures;
        return Pass::Answered;
    }

    const SaveSlotRule rule{
        .size = s.size_bytes,
        .growable = s.growable,
        .deferred_create = s.deferred_create
    };
    const SaveSlotRule::Plan plan = rule.plan_write(req.lba, block_size, sz);
    if (plan.act == SaveSlotRule::Act::Discard) {
        ++diag_.discarded_writes;
        return Pass::Answered;
    }
    if (plan.act == SaveSlotRule::Act::Create) {
        if (res) {
            auto created = res->create(req.slot, chan_->half(req.slot, drain_half).first(plan.len));
            if (!created) {
                ++diag_.write_failures;
                return Pass::Answered;
            }
            s.size_bytes = *created;
            s.file_bytes = *created;
            s.deferred_create = false;
            return Pass::Answered;
        }

        if (!submit_create(req.slot, drain_half, plan.len))
            ++diag_.write_failures;
        return Pass::Answered;
    }
    const std::uint64_t offset = plan.offset;
    const std::uint32_t len = plan.len;
    if (!res) {
        if (!submit_write(req.slot, drain_half, offset, len))
            ++diag_.write_failures;
        return Pass::Answered;
    }
    auto written = res->write_at(req.slot, offset, chan_->half(req.slot, drain_half).first(len));
    if (!written || *written != len) {
        ++diag_.write_failures;
        return Pass::Answered;
    }

    if (const std::uint64_t end = offset + len; end > s.size_bytes.v) {
        s.size_bytes = FileSize{end};
    }
    return Pass::Answered;
}

Ex<BlockSlots::Pass> BlockSlots::serve(hal::ISpiTransport& link, const SpiBlockPoll::Decode& d) {

    if (((static_cast<unsigned>(lw_slots_) >> d.req.slot.v) & 1u) != 0u) {
        ++diag_.lw_slot_passes;
        return Pass::Deferred;
    }

    if (records_[d.req.slot.v].gate.state != SlotGate::State::Idle) {
        ++diag_.deferred_passes;
        return Pass::Deferred;
    }

    const infra::OptRef<IResidentImageSource> res = resident_for(d.req.slot);

    auto pass = d.req.write ? serve_write(link, d.req, d.block_size, d.ack, res)
                            : serve_read(link, d.req, d.block_size, d.window_blocks, d.ack, res);
    if (pass && *pass == Pass::Answered)
        records_[d.req.slot.v].no_half_due_ns = 0;
    return pass;
}

}
