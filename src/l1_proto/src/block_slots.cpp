// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/block_slots.h"
#include "proto/save_slot_rule.h"
#include "proto/disc_announce.h"
#include "proto/spi_frame.h"
#include "hal/selected.h"
#include "proto/storage_completion_dispatch.h"
#include "proto/uio_opcodes.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace mister::proto {

namespace {

enum : std::uint8_t { kFillByte = 0xFF };

constexpr std::array<std::uint8_t, kBlockStagingBytes> kZeroPage{};
constexpr std::array<std::uint8_t, kBlockStagingBytes> kFfPage = [] {
    std::array<std::uint8_t, kBlockStagingBytes> a{};
    a.fill(kFillByte);
    return a;
}();

enum : std::int64_t { kNsPerUs = 1000, kMaxLateUs = std::numeric_limits<std::uint32_t>::max() };

[[nodiscard]] Error submit_refusal(Errc code, SlotIndex slot) noexcept {
    return Error{code, ERR_SITE(), slot.v};
}

}

BlockSlots::BlockSlots(hal::ISpiTransport& link) noexcept : BlockSlots(Wiring{.link = link}) {}

BlockSlots::BlockSlots(Wiring w) noexcept
    : link_(w.link),
      chan_(w.channel),
      clock_(w.clock),
      source_(w.source) {}

Ex<void> BlockSlots::announce(SlotIndex slot, FileSize size_bytes, bool writable) {
    hal::ISpiTransport& link = link_;
    if (auto r = block_poll_.send_config(); !r)
        return r;

    SpiFrame frame{link};
    if (auto r = frame.begin(UioOp::SetSdInfo); !r)
        return r;
    const std::uint32_t lo = size_bytes.low_word();
    const std::uint32_t hi = size_bytes.high_word();

    const hal::Width framing = link.io_version() != hal::IoVersion{0} ? hal::Width::Word
                                                                      : hal::Width::Byte;
    if (auto r = emit_u32(frame, lo, framing); !r)
        return r;
    if (auto r = emit_u32(frame, hi, framing); !r)
        return r;
    if (auto r = frame.end(); !r)
        return r;

    if (slot > kLastAnnounceableSlot) {
        ++diag_.unencodable_announce;
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), slot.v});
    }
    const SdStatParameter parm = SdStatParameter::of(slot, writable);
    if (auto r = frame.begin(UioOp::SetSdStat); !r)
        return r;
    if (auto r = frame.word(parm.v); !r)
        return r;
    return frame.end();
}

void BlockSlots::mount_row_(
    SlotIndex slot,
    const SlotAttributes& attr,
    MountSizes sizes,
    PathId path_id
) noexcept {
    Slot& s = records_[slot].slot;
    s.state = Slot::State::Mounted;
    s.block_size = BlockSize{kDefaultBlockBytes};
    s.size_bytes = sizes.size_bytes;
    s.file_bytes = sizes.file_bytes;
    s.path_id = path_id;
    s.writable = attr.writable;
    s.growable = attr.growable;
    s.deferred_create = attr.deferred_create;
}

Ex<void> BlockSlots::mount(
    SlotIndex slot,
    FileSize size_bytes,
    PathId path_id,
    IBlockRoles& roles
) {
    if (!slot.below<kBlockSlots>()) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot.v});
    }

    const infra::OptRef<IImageSource> src = source_of_(backing_(roles, slot));
    const SlotAttributes attr = src ? src->attributes(slot) : SlotAttributes{};

    FileSize file_bytes = size_bytes;
    FileSize extent = size_bytes;
    if (attr.deferred_create) {
        file_bytes = FileSize{0};
        extent = FileSize{0};
    } else if (attr.channel_bytes != 0) {
        extent = FileSize{attr.channel_bytes};
    }
    const MountSizes sizes{.file_bytes = file_bytes, .size_bytes = extent};
    mount_row_(slot, attr, sizes, path_id);

    records_[slot].drop_window();

    return announce(slot, size_bytes, attr.writable);
}

Ex<void> BlockSlots::unmount(SlotIndex slot) {
    if (!slot.below<kBlockSlots>()) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot.v});
    }

    records_[slot].slot = Slot{};
    records_[slot].drop_window();
    return announce(slot, FileSize{0}, false);
}

Ex<void> BlockSlots::install_disc(SlotIndex slot, FileSize size_bytes, IBlockRoles& roles) {
    if (!slot.below<kBlockSlots>()) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot.v});
    }
    if (size_bytes.empty()) {
        records_[slot].slot = Slot{};
    } else {
        const infra::OptRef<IImageSource> src = source_of_(backing_(roles, slot));
        const SlotAttributes attr = src ? src->attributes(slot) : SlotAttributes{};

        const MountSizes sizes{.file_bytes = size_bytes, .size_bytes = size_bytes};
        mount_row_(slot, attr, sizes, PathId{});
    }

    records_[slot].drop_window();
    return {};
}

const BlockSlots::Slot& BlockSlots::slot(SlotIndex i) const {
    if (!i.below<kBlockSlots>()) {
        fatal(Error{Errc::slot_range, ERR_SITE(), i.v}, "block slot index");
    }
    return records_[i].slot;
}

void BlockSlots::invalidate(SlotIndex i) {
    if (!i.below<kBlockSlots>()) {
        fatal(Error{Errc::slot_range, ERR_SITE(), i.v}, "block slot index");
    }
    records_[i].drop_window();
}

os::RawMonotonicClock::time_point BlockSlots::now_at_() const noexcept {
    return clock_ ? clock_->now() : os::RawMonotonicClock::time_point{};
}

void BlockSlots::note_answer_wait_(std::int64_t waited_ns) noexcept {
    if (waited_ns <= kLateAnswerNs)
        return;
    ++diag_.late_answers;
    const std::int64_t us = std::min<std::int64_t>(waited_ns / kNsPerUs, kMaxLateUs);
    late_max_us_ = std::max(late_max_us_, static_cast<std::uint32_t>(us));
}

BlockSlots::Refill BlockSlots::refill_resident(
    SlotIndex slot,
    Lba base,
    BlockSize block_size,
    IBlockServer& src,
    bool answer_owed
) {
    Window& w = records_[slot].window;

    const ArenaHalf served = w.half;
    w = Window{};
    w.half = served;
    const Slot& s = records_[slot].slot;
    if (s.state != Slot::State::Mounted || s.size_bytes.v == 0) {
        return Refill::Miss;
    }

    if (!chan_)
        return Refill::Miss;
    const ArenaHalf half = records_[slot].fill_half();
    const std::span<std::uint8_t> dst = chan_->half_mut(slot, half);
    if (dst.empty())
        return Refill::Miss;

    const std::uint64_t offset = block_size.position_of(base);
    auto n = src.read_at(slot, offset, dst);
    if (!n) {

        if (n.error().code == Errc::would_block && answer_owed && clock_.has_value()) {

            const auto now = now_at_();
            if (records_[slot].resident_due == kNoDue)
                records_[slot].resident_due = due_after(
                    now,
                    std::chrono::nanoseconds{kAnswerDeadlineNs}
                );
            if (now < records_[slot].resident_due)
                return Refill::Defer;
            ++diag_.answer_expiries;
            note_answer_wait_(kAnswerDeadlineNs);
        }
        records_[slot].resident_due = kNoDue;
        return Refill::Miss;
    }
    if (records_[slot].resident_due != kNoDue)
        note_answer_wait_((now_at_()
                           - (records_[slot].resident_due
                              - std::chrono::nanoseconds{kAnswerDeadlineNs}))
                              .count());
    records_[slot].resident_due = kNoDue;
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

BlockSlots::SlotGate BlockSlots::fetching_gate_(
    SlotIndex slot,
    StorageSeq seq,
    Lba base,
    BlockSize block_size,
    bool answer_owed
) const noexcept {
    const std::int64_t bound_ns = answer_owed ? kAnswerDeadlineNs : kPrefetchDeadlineNs;
    return SlotGate{
        .state = SlotGate::State::Fetching,
        .seq = seq,
        .epoch = records_[slot].epoch,
        .base = base,
        .block_size = block_size,
        .due = due_after(now_at_(), std::chrono::nanoseconds{bound_ns}),
        .answer_owed = answer_owed
    };
}

BlockSlots::SlotGate BlockSlots::staging_gate_(SlotIndex slot, StorageSeq seq) const noexcept {
    return SlotGate{
        .state = SlotGate::State::Staging,
        .seq = seq,
        .epoch = records_[slot].epoch,
        .due = due_after(now_at_(), std::chrono::nanoseconds{kStagingDeadlineNs})
    };
}

std::optional<StorageSeq> BlockSlots::submit_(const StorageRequest& r) noexcept {
    const std::optional<StorageSeq> seq = chan_->submit(r);
    if (seq)
        next_seq_ = seq->next();
    return seq;
}

bool BlockSlots::submit_fill(
    SlotIndex slot,
    Lba base,
    BlockSize block_size,
    bool answer_owed,
    const BlockBacking& backing
) noexcept {
    Window& w = records_[slot].window;
    const ArenaHalf served = w.half;
    w = Window{};
    w.half = served;
    const Slot& s = records_[slot].slot;
    if (s.state != Slot::State::Mounted || s.size_bytes.v == 0)
        return false;
    if (!storage_live_ || !chan_ || !source_of_(backing))
        return false;
    if (records_[slot].gate.state != SlotGate::State::Idle)
        return false;

    const ArenaHalf half = records_[slot].fill_half();
    if (!half.exists())
        return false;

    const std::optional<StorageSeq> seq = submit_(
        infra::make<StorageRequest>(
            StorageRequest::Read{
                .offset = block_size.position_of(base),
                .bytes = kBlockStagingBytes,
                .half = half
            },
            request_head_(slot, backing)
        )
    );
    if (!seq)
        return false;
    records_[slot].lend_half(half);
    records_[slot].gate = fetching_gate_(slot, *seq, base, block_size, answer_owed);
    return true;
}

StorageRequest::Head BlockSlots::request_head_(
    SlotIndex slot,
    const BlockBacking& backing
) const noexcept {
    StorageRequest::Head h{next_seq_, slot, ImageTag{}, {}};
    if (const auto src = source_of_(backing))
        h.tag = src->request_tag(slot);
    return h;
}

bool BlockSlots::submit_create(
    SlotIndex slot,
    ArenaHalf half,
    std::uint32_t len,
    const BlockBacking& backing
) noexcept {
    if (!storage_live_ || !chan_)
        return false;
    if (!half.exists())
        return false;
    if (records_[slot].gate.state != SlotGate::State::Idle)
        return false;
    const std::optional<StorageSeq> seq = submit_(
        infra::make<StorageRequest>(
            StorageRequest::Create{.bytes = len, .half = half},
            request_head_(slot, backing)
        )
    );
    if (!seq)
        return false;
    gate_commit_(slot, *seq, half, 0, len);
    return true;
}

bool BlockSlots::submit_write(
    SlotIndex slot,
    ArenaHalf half,
    std::uint64_t offset,
    std::uint32_t len,
    const BlockBacking& backing
) noexcept {
    if (!storage_live_ || !chan_)
        return false;
    if (!half.exists())
        return false;
    if (records_[slot].gate.state != SlotGate::State::Idle)
        return false;
    const std::optional<StorageSeq> seq = submit_(
        infra::make<StorageRequest>(
            StorageRequest::Write{.offset = offset, .bytes = len, .half = half},
            request_head_(slot, backing)
        )
    );
    if (!seq)
        return false;
    gate_commit_(slot, *seq, half, offset, len);
    return true;
}

void BlockSlots::gate_commit_(
    SlotIndex slot,
    StorageSeq seq,
    ArenaHalf half,
    std::uint64_t offset,
    std::uint32_t len
) noexcept {
    records_[slot].lend_half(half);
    records_[slot].gate = SlotGate{
        .state = SlotGate::State::Committing,
        .seq = seq,
        .epoch = records_[slot].epoch,
        .offset = offset,
        .len = len,
        .due = due_after(now_at_(), std::chrono::nanoseconds{kAnswerDeadlineNs})
    };
}

Ex<void> BlockSlots::submit_attach(SlotIndex slot, IBlockRoles& roles) noexcept {
    if (!slot.below<kBlockSlots>())
        return std::unexpected(submit_refusal(Errc::slot_range, slot));
    if (!storage_live_ || !chan_)
        return std::unexpected(submit_refusal(Errc::seat_absent, slot));
    if (records_[slot].gate.state != SlotGate::State::Idle)
        return std::unexpected(submit_refusal(Errc::busy, slot));

    const BlockBacking backing = backing_(roles, slot);
    const infra::OptRef<IImageSource> src = source_of_(backing);
    const bool growable = src && src->attributes(slot).growable;
    return submit_lifecycle_(
        slot,
        infra::make<StorageRequest>(
            StorageRequest::Attach{.growable = growable},
            request_head_(slot, backing)
        )
    );
}

Ex<void> BlockSlots::submit_detach(SlotIndex slot, IBlockRoles& roles) noexcept {
    if (!slot.below<kBlockSlots>())
        return std::unexpected(submit_refusal(Errc::slot_range, slot));
    if (!storage_live_ || !chan_)
        return std::unexpected(submit_refusal(Errc::seat_absent, slot));
    if (records_[slot].gate.state != SlotGate::State::Idle)
        return std::unexpected(submit_refusal(Errc::busy, slot));
    return submit_lifecycle_(
        slot,
        infra::make<StorageRequest>(
            StorageRequest::Detach{},
            request_head_(slot, backing_(roles, slot))
        )
    );
}

Ex<void> BlockSlots::submit_lifecycle_(SlotIndex slot, const StorageRequest& r) noexcept {
    const std::optional<StorageSeq> seq = submit_(r);
    if (!seq)
        return std::unexpected(submit_refusal(Errc::would_block, slot));
    records_[slot].gate = staging_gate_(slot, *seq);
    return {};
}

static_assert(StorageCompletionSink<BlockSlots, IBlockRoles>);

std::optional<SlotIndex> BlockSlots::slot_of_(const StorageCompletion::Head& h) noexcept {
    if (h.slot.below<kBlockSlots>())
        return h.slot;
    ++diag_.stale_completions;
    return std::nullopt;
}

bool BlockSlots::stale_(SlotIndex slot, const StorageCompletion::Head& h) noexcept {
    const SlotGate& p = records_[slot].gate;
    if (p.state == SlotGate::State::Idle || p.seq != h.seq) {
        ++diag_.stale_completions;
        return true;
    }
    return false;
}

void BlockSlots::on(
    const StorageCompletion::Read& a,
    const StorageCompletion::Head& h,
    IBlockRoles&
) noexcept {
    const auto at = slot_of_(h);
    if (!at)
        return;
    const SlotIndex slot = *at;
    records_[slot].reclaim_half(a.half);
    if (stale_(slot, h))
        return;
    const SlotGate& p = records_[slot].gate;
    bool blank_next = false;
    if (p.epoch != records_[slot].epoch) {

        ++diag_.stale_completions;
    } else if (h.status == StorageStatus::Ok && a.bytes != 0) {
        Window& w = records_[slot].window;
        w.base = p.base;
        w.valid_bytes = a.bytes > kBlockStagingBytes ? kBlockStagingBytes : a.bytes;
        w.block_size = p.block_size;
        w.half = a.half;
    } else if (p.answer_owed) {
        blank_next = true;
    }
    records_[slot].finish_gate(blank_next);
}

void BlockSlots::on(
    const StorageCompletion::Write& a,
    const StorageCompletion::Head& h,
    IBlockRoles&
) noexcept {
    const auto at = slot_of_(h);
    if (!at)
        return;
    const SlotIndex slot = *at;
    records_[slot].reclaim_half(a.half);
    if (stale_(slot, h))
        return;
    const SlotGate& p = records_[slot].gate;
    Slot& s = records_[slot].slot;
    if (p.epoch != records_[slot].epoch) {

        ++diag_.stale_completions;
    } else if (h.status == StorageStatus::Ok && a.bytes == p.len) {

        if (const std::uint64_t end = p.offset + p.len; end > s.size_bytes.v) {
            s.size_bytes = FileSize{end};
        }
    } else {
        ++diag_.write_failures;
    }
    records_[slot].finish_gate(false);
}

void BlockSlots::on(
    const StorageCompletion::Create& a,
    const StorageCompletion::Head& h,
    IBlockRoles&
) noexcept {
    const auto at = slot_of_(h);
    if (!at)
        return;
    const SlotIndex slot = *at;
    records_[slot].reclaim_half(a.half);
    if (stale_(slot, h))
        return;
    Slot& s = records_[slot].slot;
    if (records_[slot].gate.epoch != records_[slot].epoch) {
        ++diag_.stale_completions;
    } else if (h.status == StorageStatus::Ok) {
        s.size_bytes = FileSize{a.size};
        s.file_bytes = FileSize{a.size};
        s.deferred_create = false;
    } else {
        ++diag_.write_failures;
    }
    records_[slot].finish_gate(false);
}

void BlockSlots::on(
    const StorageCompletion::Flush&,
    const StorageCompletion::Head& h,
    IBlockRoles&
) noexcept {
    const auto at = slot_of_(h);
    if (!at)
        return;
    const SlotIndex slot = *at;
    if (stale_(slot, h))
        return;
    ++diag_.stale_completions;
}

void BlockSlots::on(
    const StorageCompletion::Attach& a,
    const StorageCompletion::Head& h,
    IBlockRoles& roles
) noexcept {
    const auto at = slot_of_(h);
    if (!at)
        return;
    const SlotIndex slot = *at;
    if (const auto src = source_of_(backing_(roles, slot))) {
        const bool ok = h.status == StorageStatus::Ok;
        src->note_attached(slot, ok, FileSize{ok ? a.size : 0});
    }
    if (stale_(slot, h))
        return;
    records_[slot].finish_gate(false);
}

void BlockSlots::on(
    const StorageCompletion::Detach&,
    const StorageCompletion::Head& h,
    IBlockRoles& roles
) noexcept {
    const auto at = slot_of_(h);
    if (!at)
        return;
    const SlotIndex slot = *at;
    if (const auto src = source_of_(backing_(roles, slot)))
        src->note_detached(slot);
    if (stale_(slot, h))
        return;
    records_[slot].finish_gate(false);
}

void BlockSlots::misrouted(const StorageCompletion&, IBlockRoles&) noexcept {
    TASTY_SEAT_BODY(BlockSlots);
    ++diag_.stale_completions;
}

void BlockSlots::expire(SlotIndex slot) noexcept {
    SlotGate& p = records_[slot].gate;
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

unsigned BlockSlots::install_completions(IBlockRoles& roles) noexcept {
    if (!chan_)
        return 0;
    unsigned n = 0;
    const os::RawMonotonicClock::time_point now = now_at_();

    while (const auto c = chan_->reap()) {
        ++n;
        infra::dispatch<StorageCompletionRoutes>(*c, *this, roles);
    }
    for (unsigned i = 0; i < kBlockSlots; ++i) {
        const SlotIndex slot = SlotIndex::of(i);
        const SlotGate& p = records_[slot].gate;
        if (p.state != SlotGate::State::Idle && clock_.has_value() && now >= p.due) {
            expire(slot);
        }
    }
    return n;
}

BlockSlots::Blank BlockSlots::no_half_answer(SlotIndex slot) noexcept {
    os::RawMonotonicClock::time_point& due = records_[slot].no_half_due;
    if (!clock_) {
        ++diag_.no_half_substitutes;
        due = kNoDue;
        return Blank::Static;
    }
    const os::RawMonotonicClock::time_point now = now_at_();
    if (due == kNoDue)
        due = due_after(now, std::chrono::nanoseconds{kAnswerDeadlineNs});
    if (now < due)
        return Blank::Deferred;
    due = kNoDue;
    ++diag_.no_half_substitutes;
    return Blank::Static;
}

BlockSlots::Blank BlockSlots::substitute_blank(
    SlotIndex slot,
    Lba lba,
    const BlockBacking& backing
) {

    if (chan_ && records_[slot].half_held(records_[slot].window.half)) {
        const ArenaHalf free_half = records_[slot].fill_half();

        if (!free_half.exists())
            return no_half_answer(slot);
        records_[slot].window.half = free_half;
    }
    records_[slot].no_half_due = kNoDue;
    ++diag_.blank_filled;
    if (!chan_)
        return Blank::NoBuffer;
    std::span<std::uint8_t> window = chan_->half_mut(slot, records_[slot].window.half);
    if (window.empty())
        return Blank::NoBuffer;
    if (records_[slot].slot.deferred_create) {
        const infra::OptRef<IImageSource> src = source_of_(backing);
        if (src && src->fill_blank(slot, lba, window))
            return Blank::Filled;
        std::memset(window.data(), kFillByte, window.size());
        return Blank::Filled;
    }
    std::memset(window.data(), 0x00, window.size());
    return Blank::Filled;
}

[[nodiscard]] Ex<void> BlockSlots::emit_read_data_phase_(
    SlotIndex slot,
    std::span<const std::uint8_t> serve,
    std::size_t offset,
    std::size_t nbytes,
    SectorAck ack,
    BlockCount blks,
    bool count_blocks
) {
    hal::ISpiTransport& link = link_;
    hal::Selected cs(link, hal::ChipSelect::Io);

    if (auto r = link.transfer(ack.with_opcode(UioOp::SectorRd)); !r) {
        return std::unexpected(r.error());
    }
    if (serve.size() < offset + nbytes) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), slot.v});
    }
    if (auto r = link.block_write(serve.subspan(offset, nbytes)); !r) {
        return std::unexpected(r.error());
    }

    if (count_blocks)
        diag_.blocks_served += blks.v;
    return {};
}

Ex<bool> BlockSlots::answer_read(SlotIndex slot, Lba lba, std::uint32_t nbytes, SectorAck ack) {
    if (!slot.below<kBlockSlots>() || nbytes == 0)
        return false;

    if (records_[slot].gate.state != SlotGate::State::Idle)
        return false;
    const Window& w = records_[slot].window;

    const BlockSize block_size = w.block_size;
    const BlockCount window_blocks = block_size.blocks_in(BlockSize{kBlockStagingBytes});
    const BlockCount blks = block_size.blocks_in(BlockSize{nbytes});

    if (block_size.extent(blks) != FileSize{nbytes})
        return false;
    const bool hit = w.valid_bytes != 0 && w.covers(lba, blks, window_blocks);
    if (!hit)
        return false;

    const std::uint32_t offset = w.block_size.offset_of(lba - w.base);
    const std::span<const std::uint8_t> serve = chan_ ? chan_->half(slot, w.half)
                                                      : std::span<const std::uint8_t>{};
    if (auto r = emit_read_data_phase_(slot, serve, offset, nbytes, ack, blks, true); !r) {
        return std::unexpected(r.error());
    }
    return true;
}

Ex<BlockSlots::Pass> BlockSlots::serve_read(
    const SdRequest& req,
    BlockSize block_size,
    BlockCount window_blocks,
    SectorAck ack,
    const BlockBacking& backing
) {
    const BlockCount blks = req.block_count;
    const std::size_t sz = block_size.length_of(blks);
    Window& w = records_[req.slot].window;

    const infra::OptRef<IBlockServer> res = backing.server();

    const bool hit = w.valid_bytes != 0
        && w.block_size == block_size
        && w.covers(req.lba, blks, window_blocks);

    std::uint32_t offset = 0;
    bool cached = hit;
    Blank blank = Blank::Filled;
    if (hit) {
        offset = w.block_size.offset_of(req.lba - w.base);
    } else if (res) {
        const Refill r = refill_resident(req.slot, req.lba, block_size, *res, true);
        cached = r == Refill::Hit;
        if (r == Refill::Defer)
            blank = Blank::Deferred;
        else if (r == Refill::Miss)
            blank = substitute_blank(req.slot, req.lba, backing);
    } else if (records_[req.slot].gate.blank_next) {

        blank = substitute_blank(req.slot, req.lba, backing);
        if (blank != Blank::Deferred)
            records_[req.slot].gate.blank_next = false;
    } else if (submit_fill(req.slot, req.lba, block_size, true, backing)) {

        ++diag_.deferred_passes;
        return Pass::Deferred;
    } else {
        blank = substitute_blank(req.slot, req.lba, backing);
    }
    if (blank == Blank::Deferred) {
        ++diag_.deferred_passes;
        return Pass::Deferred;
    }

    const std::span<const std::uint8_t> serve = blank == Blank::Static
        ? std::span<
              const std::uint8_t>{records_[req.slot].slot.deferred_create ? kFfPage : kZeroPage}
        : chan_ ? chan_->half(req.slot, records_[req.slot].window.half)
                : std::span<const std::uint8_t>{};
    if (auto r = emit_read_data_phase_(req.slot, serve, offset, sz, ack, blks, cached); !r) {
        return std::unexpected(r.error());
    }

    if (records_[req.slot].slot.deferred_create) {
        records_[req.slot].window = Window{};
        return Pass::Answered;
    }
    if (!cached)
        return Pass::Answered;

    if ((req.lba + req.block_count) - records_[req.slot].window.base == window_blocks) {
        const Lba next = req.lba + req.block_count;
        if (res) {

            if (refill_resident(req.slot, next, block_size, *res, false) != Refill::Hit) {
                if (chan_) {
                    const auto win = chan_->half_mut(req.slot, records_[req.slot].window.half);
                    if (!win.empty())
                        std::memset(win.data(), 0, win.size());
                }
            }
        } else {

            (void)submit_fill(req.slot, next, block_size, false, backing);
        }
    }
    return Pass::Answered;
}

Ex<BlockSlots::Pass> BlockSlots::serve_write(
    const SdRequest& req,
    BlockSize block_size,
    SectorAck ack,
    const BlockBacking& backing
) {
    hal::ISpiTransport& link = link_;
    const std::size_t sz = block_size.length_of(req.block_count);

    const infra::OptRef<IBlockServer> res = backing.server();

    if (!chan_) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), req.slot.v});
    }
    const ArenaHalf drain_half = records_[req.slot].fill_half();
    if (!drain_half.exists()) {
        ++diag_.deferred_passes;
        return Pass::Deferred;
    }

    records_[req.slot].window = Window{};
    {
        hal::Selected cs(link, hal::ChipSelect::Io);

        if (auto r = link.transfer(ack.with_opcode(UioOp::SectorWr)); !r) {
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

    Slot& s = records_[req.slot].slot;
    const infra::OptRef<IImageSource> src = source_of_(backing);
    if (!src || s.state != Slot::State::Mounted) {
        ++diag_.write_failures;
        return Pass::Answered;
    }

    const SaveSlotRule rule{
        .size = s.size_bytes,
        .growable = s.growable,
        .deferred_create = s.deferred_create
    };

    const SaveSlotRule::Plan plan = rule.plan_write(
        req.lba,
        block_size,
        static_cast<std::uint32_t>(sz)
    );
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

        if (!submit_create(req.slot, drain_half, plan.len, backing))
            ++diag_.write_failures;
        return Pass::Answered;
    }
    const std::uint64_t offset = plan.offset;
    const std::uint32_t len = plan.len;
    if (!res) {
        if (!submit_write(req.slot, drain_half, offset, len, backing))
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

Ex<BlockSlots::Pass> BlockSlots::serve(const SpiBlockPoll::Decode& d, IBlockRoles& roles) {

    if (lw_slots_.has(d.req.slot)) {
        ++diag_.lw_slot_passes;
        return Pass::Deferred;
    }

    if (records_[d.req.slot].gate.state != SlotGate::State::Idle) {
        ++diag_.deferred_passes;
        return Pass::Deferred;
    }

    const BlockBacking backing = backing_(roles, d.req.slot);
    auto pass = d.req.write ? serve_write(d.req, d.block_size, d.ack, backing)
                            : serve_read(d.req, d.block_size, d.window_blocks, d.ack, backing);
    if (pass && *pass == Pass::Answered)
        records_[d.req.slot].no_half_due = kNoDue;
    return pass;
}

Ex<SpiBlockPoll::Decode> BlockSlots::poll_block(
    IBlockRoles& roles,
    std::optional<SdStatusWord> known
) {
    return block_poll_.poll(roles, server_, known);
}

}
