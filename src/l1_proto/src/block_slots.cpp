// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/block_slots.h"

#include "hal/disk_lamp.h"
#include "infra/message_sum.h"
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

constexpr std::int64_t kMaxLateUs = std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] Error submit_refusal(Errc code, SlotIndex slot) noexcept {
    return Error{code, ERR_SITE(), slot.v};
}

struct CreditRow {
    StorageRequest::Kind kind;
    std::chrono::nanoseconds bound;
    std::uint32_t BlockSlots::Diagnostics::* expiry;
    bool owes_answer;
};

constexpr std::chrono::nanoseconds kAnswer{BlockSlots::kAnswerDeadlineNs};
constexpr std::chrono::nanoseconds kPrefetch{BlockSlots::kPrefetchDeadlineNs};
constexpr std::chrono::nanoseconds kStaging{BlockSlots::kStagingDeadlineNs};
constexpr std::array<CreditRow, infra::ordinal(StorageRequest::Kind::kCount)> kCreditRows{{
    {StorageRequest::Kind::Fetch, kAnswer, &BlockSlots::Diagnostics::answer_expiries, true},
    {StorageRequest::Kind::ReadAhead,
     kPrefetch,
     &BlockSlots::Diagnostics::prefetch_expiries,
     false},
    {StorageRequest::Kind::Write, kAnswer, &BlockSlots::Diagnostics::write_failures, false},
    {StorageRequest::Kind::Create, kAnswer, &BlockSlots::Diagnostics::write_failures, false},
    {StorageRequest::Kind::Flush, kStaging, nullptr, false},
    {StorageRequest::Kind::Attach, kStaging, &BlockSlots::Diagnostics::staging_expiries, false},
    {StorageRequest::Kind::Detach, kStaging, &BlockSlots::Diagnostics::staging_expiries, false},
}};
static_assert(
    infra::rows_are_ordinal(kCreditRows),
    "one row per StorageRequest::Kind, in enumerator order"
);

[[nodiscard]] constexpr std::chrono::nanoseconds bound_of(StorageRequest::Kind k) noexcept {
    return kCreditRows[infra::ordinal(k)].bound;
}

}

BlockSlots::BlockSlots(hal::ISpiTransport& link) noexcept : BlockSlots(Wiring{.link = link}) {}

BlockSlots::BlockSlots(Wiring w) noexcept
    : link_(w.link),
      chan_(w.channel),
      clock_(w.clock),
      source_(w.source),
      lamp_(w.lamp) {}

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

    drop_(slot);

    return announce(slot, size_bytes, attr.writable);
}

Ex<void> BlockSlots::unmount(SlotIndex slot) {
    if (!slot.below<kBlockSlots>()) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot.v});
    }

    records_[slot].slot = Slot{};
    drop_(slot);
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

    drop_(slot);
    return {};
}

void BlockSlots::change_disc(SlotIndex slot) noexcept {
    TASTY_SEAT_BODY(BlockSlots);
    if (!slot.below<kBlockSlots>())
        return;
    Slot& s = records_[slot].slot;
    if (s.state != Slot::State::Mounted)
        return;
    s.state = Slot::State::Changing;
    drop_(slot);
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
    drop_(i);
}

void BlockSlots::invalidate_all() noexcept {
    for (std::uint8_t i = 0; i < kBlockSlots; ++i)
        drop_(SlotIndex{i});
}

void BlockSlots::drop_(SlotIndex slot) noexcept {
    records_[slot].epoch = records_[slot].epoch.next();
    end_window_(slot);
    records_[slot].change_due = kNoDue;
}

void BlockSlots::end_window_(SlotIndex slot) noexcept {
    if (chan_)
        chan_->install(slot, ResidentWindow{});
}

os::RawMonotonicClock::time_point BlockSlots::now_at_() const noexcept {
    return clock_ ? clock_->now() : os::RawMonotonicClock::time_point{};
}

void BlockSlots::note_answer_wait_(std::chrono::nanoseconds waited) noexcept {
    if (waited <= std::chrono::nanoseconds{kLateAnswerNs})
        return;
    ++diag_.late_answers;
    const std::int64_t us = std::min<std::int64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(waited).count(),
        kMaxLateUs
    );
    late_max_us_ = std::max(late_max_us_, static_cast<std::uint32_t>(us));
}

BlockSlots::Gate BlockSlots::gate_(SlotIndex slot, os::RawMonotonicClock::time_point now) noexcept {
    if (!chan_)
        return Gate::Open;
    const IStorageChannel::HandsOut out = chan_->hands_out();
    SlotRecord& r = records_[slot];
    const bool clocked = clock_.has_value();
    if (out.ctl.has(slot) && r.ctl.phase == SlotRecord::Credit::Phase::Live) {
        if (!clocked || !r.ctl.overdue(now))
            return Gate::Defer;
        expire_(r.ctl);
    }
    if (out.data.has(slot)) {
        SlotRecord::Credit& c = r.data;
        if (c.phase == SlotRecord::Credit::Phase::Live) {
            if (!clocked || !c.overdue(now))
                return Gate::Defer;
            expire_(c);
        }

        if (!clocked || now >= c.due)
            return Gate::Substitute;
        return Gate::Defer;
    }
    return Gate::Open;
}

void BlockSlots::expire_(SlotRecord::Credit& c) noexcept {
    const CreditRow& row = kCreditRows[infra::ordinal(c.kind)];
    if (row.expiry != nullptr)
        ++(diag_.*(row.expiry));

    if (row.owes_answer)
        note_answer_wait_(row.bound);
    c.phase = SlotRecord::Credit::Phase::Expired;
}

bool BlockSlots::slot_busy(SlotIndex slot) const noexcept {
    if (!chan_ || !slot.below<kBlockSlots>())
        return false;
    const IStorageChannel::HandsOut out = chan_->hands_out();
    const SlotRecord& r = records_[slot];
    return (out.data.has(slot) && r.data.phase == SlotRecord::Credit::Phase::Live)
        || (out.ctl.has(slot) && r.ctl.phase == SlotRecord::Credit::Phase::Live);
}

std::optional<StorageSeq> BlockSlots::submit_(const StorageRequest& r) noexcept {
    const std::optional<StorageSeq> seq = chan_->submit(r);
    if (seq)
        next_seq_ = seq->next();
    return seq;
}

bool BlockSlots::submit_fetch(
    SlotIndex slot,
    Lba base,
    BlockSize block_size,
    bool answer_owed,
    const BlockBacking& backing
) noexcept {
    const Slot& s = records_[slot].slot;
    if (s.state != Slot::State::Mounted)
        return false;

    if (s.size_bytes.v == 0 && !s.deferred_create)
        return false;
    if (!storage_live_ || !chan_ || !source_of_(backing))
        return false;

    const StorageRequest::Kind kind = answer_owed ? StorageRequest::Kind::Fetch
                                                  : StorageRequest::Kind::ReadAhead;
    const std::optional<StorageSeq> seq = answer_owed
        ? submit_(
              infra::make<StorageRequest>(
                  StorageRequest::Fetch{.base = base, .block_size = block_size},
                  request_head_(slot, backing)
              )
          )
        : submit_(
              infra::make<StorageRequest>(
                  StorageRequest::ReadAhead{.base = base, .block_size = block_size},
                  request_head_(slot, backing)
              )
          );
    if (!seq)
        return false;
    records_[slot].data = SlotRecord::Credit{
        .kind = kind,
        .epoch = records_[slot].epoch,
        .due = due_after(now_at_(), bound_of(kind)),
        .phase = SlotRecord::Credit::Phase::Live
    };
    if (lamp_)
        lamp_->note();
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
    std::uint32_t len,
    const BlockBacking& backing
) noexcept {
    if (!storage_live_ || !chan_)
        return false;
    const std::optional<StorageSeq> seq = submit_(
        infra::make<StorageRequest>(
            StorageRequest::Create{.bytes = len},
            request_head_(slot, backing)
        )
    );
    if (!seq)
        return false;
    records_[slot].data = SlotRecord::Credit{
        .kind = StorageRequest::Kind::Create,
        .epoch = records_[slot].epoch,
        .due = due_after(now_at_(), bound_of(StorageRequest::Kind::Create)),
        .phase = SlotRecord::Credit::Phase::Live
    };
    if (lamp_)
        lamp_->note();
    return true;
}

bool BlockSlots::submit_write(
    SlotIndex slot,
    std::uint64_t offset,
    std::uint32_t len,
    const BlockBacking& backing
) noexcept {
    if (!storage_live_ || !chan_)
        return false;
    const std::optional<StorageSeq> seq = submit_(
        infra::make<StorageRequest>(
            StorageRequest::Write{.offset = offset, .bytes = len},
            request_head_(slot, backing)
        )
    );
    if (!seq)
        return false;
    records_[slot].data = SlotRecord::Credit{
        .kind = StorageRequest::Kind::Write,
        .epoch = records_[slot].epoch,
        .due = due_after(now_at_(), bound_of(StorageRequest::Kind::Write)),
        .phase = SlotRecord::Credit::Phase::Live
    };
    if (lamp_)
        lamp_->note();
    return true;
}

Ex<void> BlockSlots::submit_attach(SlotIndex slot, IBlockRoles& roles) noexcept {
    if (!slot.below<kBlockSlots>())
        return std::unexpected(submit_refusal(Errc::slot_range, slot));
    if (!storage_live_ || !chan_)
        return std::unexpected(submit_refusal(Errc::seat_absent, slot));
    if (slot_busy(slot))
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
    if (slot_busy(slot))
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

    const StorageRequest::Kind kind = infra::kind_of(r);
    records_[slot].ctl = SlotRecord::Credit{
        .kind = kind,
        .epoch = records_[slot].epoch,
        .due = due_after(now_at_(), bound_of(kind)),
        .phase = SlotRecord::Credit::Phase::Live
    };
    return {};
}

static_assert(StorageCompletionSink<BlockSlots, IBlockRoles>);

std::optional<SlotIndex> BlockSlots::slot_of_(const StorageCompletion::Head& h) noexcept {
    if (h.slot.below<kBlockSlots>())
        return h.slot;
    ++diag_.stale_completions;
    return std::nullopt;
}

void BlockSlots::on(
    const StorageCompletion::Fetch&,
    const StorageCompletion::Head& h,
    IBlockRoles&
) noexcept {
    const auto at = slot_of_(h);
    if (!at)
        return;
    const SlotIndex slot = *at;
    SlotRecord& r = records_[slot];
    if (r.data.stale(r.epoch)) {
        ++diag_.stale_completions;
        chan_->install(slot, ResidentWindow{});
        return;
    }
    const CreditRow& row = kCreditRows[infra::ordinal(r.data.kind)];
    if (row.owes_answer)
        note_answer_wait_(now_at_() - (r.data.due - row.bound));
}

void BlockSlots::on(
    const StorageCompletion::ReadAhead&,
    const StorageCompletion::Head& h,
    IBlockRoles&
) noexcept {
    const auto at = slot_of_(h);
    if (!at)
        return;
    const SlotIndex slot = *at;
    if (records_[slot].data.stale(records_[slot].epoch)) {
        ++diag_.stale_completions;
        chan_->install(slot, ResidentWindow{});
    }
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
    if (records_[slot].data.stale(records_[slot].epoch)) {

        ++diag_.stale_completions;
        return;
    }
    Slot& s = records_[slot].slot;
    if (h.status == StorageStatus::Ok) {

        if (a.end > s.size_bytes)
            s.size_bytes = a.end;
    } else {
        ++diag_.write_failures;
    }
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
    if (records_[slot].data.stale(records_[slot].epoch)) {
        ++diag_.stale_completions;
        return;
    }
    Slot& s = records_[slot].slot;
    if (h.status == StorageStatus::Ok) {
        s.size_bytes = FileSize{a.size};
        s.file_bytes = FileSize{a.size};
        s.deferred_create = false;
    } else {
        ++diag_.write_failures;
    }
}

void BlockSlots::on(
    const StorageCompletion::Flush&,
    const StorageCompletion::Head& h,
    IBlockRoles&
) noexcept {
    const auto at = slot_of_(h);
    if (!at)
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
    if (records_[slot].ctl.stale(records_[slot].epoch))
        ++diag_.stale_completions;
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
    if (records_[slot].ctl.stale(records_[slot].epoch))
        ++diag_.stale_completions;
}

void BlockSlots::misrouted(const StorageCompletion&, IBlockRoles&) noexcept {
    TASTY_SEAT_BODY(BlockSlots);
    ++diag_.stale_completions;
}

unsigned BlockSlots::install_completions(IBlockRoles& roles) noexcept {
    if (!chan_)
        return 0;
    unsigned n = 0;

    while (const auto c = chan_->reap()) {
        ++n;
        infra::dispatch<StorageCompletionRoutes>(*c, *this, roles);
    }

    if (!clock_.has_value())
        return n;
    const os::RawMonotonicClock::time_point now = now_at_();
    const IStorageChannel::HandsOut out = chan_->hands_out();
    for (std::uint8_t i = 0; i < kBlockSlots; ++i) {
        const SlotIndex slot{i};
        SlotRecord& r = records_[slot];
        if (out.data.has(slot)
            && r.data.phase == SlotRecord::Credit::Phase::Live
            && r.data.overdue(now)) {
            expire_(r.data);
        }
        if (out.ctl.has(slot)
            && r.ctl.phase == SlotRecord::Credit::Phase::Live
            && r.ctl.overdue(now)) {
            expire_(r.ctl);
        }
    }
    return n;
}

std::span<const std::uint8_t> BlockSlots::substitute_(
    SlotIndex slot,
    Lba lba,
    const BlockBacking& backing
) {
    if (!chan_)
        return {};
    if (!records_[slot].slot.deferred_create)
        return kZeroPage;
    const infra::OptRef<IImageSource> src = source_of_(backing);
    if (src && src->fill_blank(slot, lba, blank_page_))
        return blank_page_;
    return kFfPage;
}

[[nodiscard]] Ex<void> BlockSlots::emit_read_data_phase_(
    SlotIndex slot,
    std::span<const std::uint8_t> serve,
    std::size_t offset,
    FileSize nbytes,
    SectorAck ack,
    BlockCount blks,
    bool count_blocks
) {
    hal::ISpiTransport& link = link_;
    hal::Selected cs(link, hal::ChipSelect::Io);

    if (auto r = link.transfer(ack.with_opcode(UioOp::SectorRd)); !r) {
        return std::unexpected(r.error());
    }
    if (FileSize::of(serve) < FileSize{offset}.advance(nbytes)) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), slot.v});
    }
    if (auto r = link.block_write(nbytes.first_of(FileSize{offset}.skip_in(serve))); !r) {
        return std::unexpected(r.error());
    }

    if (count_blocks)
        diag_.blocks_served += blks.v;
    return {};
}

Ex<bool> BlockSlots::answer_read(SlotIndex slot, Lba lba, std::uint32_t nbytes, SectorAck ack) {
    if (!slot.below<kBlockSlots>() || nbytes == 0)
        return false;

    if (slot_busy(slot))
        return false;
    const std::optional<IStorageChannel::HomeWindow> home = chan_ ? chan_->window(slot)
                                                                  : std::nullopt;
    if (!home)
        return false;
    const BlockSize block_size = home->window.block_size;
    const BlockCount blks = block_size.blocks_in(FileSize{nbytes});

    if (block_size.extent(blks) != FileSize{nbytes})
        return false;
    const ResidentWindow::Hit hit = home->window.hit(
        ResidentWindow::Ask{lba, blks, std::nullopt},
        home->bytes
    );
    if (hit.is != ResidentWindow::Hit::Is::Image)
        return false;
    if (auto r = emit_read_data_phase_(slot, hit.bytes, 0, FileSize{nbytes}, ack, blks, true); !r) {
        return std::unexpected(r.error());
    }
    return true;
}

Ex<BlockSlots::Pass> BlockSlots::serve_read(
    const SdRequest& req,
    BlockSize block_size,
    SectorAck ack,
    const BlockBacking& backing
) {
    const SlotIndex slot = req.slot;
    const BlockCount blks = req.block_count;
    const ResidentWindow::Ask ask{req.lba, blks, block_size};

    const ReadPass pass{slot, req.lba, blks, block_size, block_size.extent(blks), ack, backing};

    if (records_[slot].slot.state == Slot::State::Changing) {
        const os::RawMonotonicClock::time_point now = now_at_();
        SlotRecord& r = records_[slot];
        if (!clock_.has_value()) {
            ++diag_.deferred_passes;
            return Pass::Deferred;
        }
        if (r.change_due == kNoDue || now < r.change_due) {
            if (r.change_due == kNoDue)
                r.change_due = due_after(now, std::chrono::nanoseconds{kAnswerDeadlineNs});
            ++diag_.deferred_passes;
            return Pass::Deferred;
        }

        r.change_due = due_after(now, std::chrono::nanoseconds{kAnswerDeadlineNs});
        return answer_read_substitute_(pass, false);
    }

    const std::optional<IStorageChannel::HomeWindow> home = chan_ ? chan_->window(slot)
                                                                  : std::nullopt;
    const ResidentWindow::Hit hit = home ? home->window.hit(ask, home->bytes)
                                         : ResidentWindow::Hit{};

    if (hit.is == ResidentWindow::Hit::Is::Image)
        return answer_read_image_(pass, hit);
    if (hit.is == ResidentWindow::Hit::Is::Substitute) {
        chan_->install(slot, ResidentWindow{});
        return answer_read_substitute_(pass, false);
    }

    const Slot& s = records_[slot].slot;
    if (s.state != Slot::State::Mounted
        || (s.size_bytes.v == 0 && !s.deferred_create)
        || !storage_live_
        || !chan_
        || !source_of_(backing)) {
        return answer_read_substitute_(pass, false);
    }
    if (submit_fetch(slot, req.lba, block_size, true, backing)) {

        ++diag_.deferred_passes;
        return Pass::Deferred;
    }
    return answer_read_substitute_(pass, false);
}

Ex<BlockSlots::Pass> BlockSlots::answer_read_image_(
    const ReadPass& p,
    const ResidentWindow::Hit& image
) {

    const bool blank = records_[p.slot].slot.deferred_create;
    if (auto r = emit_read_data_phase_(p.slot, image.bytes, 0, p.nbytes, p.ack, p.blks, !blank);
        !r) {
        return std::unexpected(r.error());
    }
    if (blank) {
        ++diag_.blank_filled;
        chan_->install(p.slot, ResidentWindow{});
        return Pass::Answered;
    }
    if (image.read_ahead)
        (void)submit_fetch(p.slot, *image.read_ahead, p.block_size, false, p.backing);
    return Pass::Answered;
}

Ex<BlockSlots::Pass> BlockSlots::answer_read_substitute_(const ReadPass& p, bool rearmed) {
    ++(rearmed ? diag_.rearmed_substitutes : diag_.blank_filled);
    if (auto r = emit_read_data_phase_(
            p.slot,
            substitute_(p.slot, p.lba, p.backing),
            0,
            p.nbytes,
            p.ack,
            p.blks,
            false
        );
        !r) {
        return std::unexpected(r.error());
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

    if (!chan_) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), req.slot.v});
    }

    const std::span<std::uint8_t> drain = chan_->claim(req.slot);

    if (drain.size() >= sz)
        std::memset(drain.data(), 0, sz);
    {
        hal::Selected cs(link, hal::ChipSelect::Io);

        if (auto r = link.transfer(ack.with_opcode(UioOp::SectorWr)); !r) {
            return std::unexpected(r.error());
        }
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

        if (!submit_create(req.slot, plan.len, backing))
            ++diag_.write_failures;
        return Pass::Answered;
    }
    if (!submit_write(req.slot, plan.offset, plan.len, backing))
        ++diag_.write_failures;
    return Pass::Answered;
}

Ex<BlockSlots::Pass> BlockSlots::serve(const SpiBlockPoll::Decode& d, IBlockRoles& roles) {

    if (lw_slots_.has(d.req.slot)) {
        ++diag_.lw_slot_passes;
        return Pass::Deferred;
    }

    const os::RawMonotonicClock::time_point now = now_at_();
    const Gate gate = gate_(d.req.slot, now);
    if (gate == Gate::Defer || (gate == Gate::Substitute && d.req.write)) {

        ++diag_.deferred_passes;
        return Pass::Deferred;
    }

    const BlockBacking backing = backing_(roles, d.req.slot);
    if (gate == Gate::Substitute) {
        SlotRecord::Credit& c = records_[d.req.slot].data;
        const bool rearmed = c.phase == SlotRecord::Credit::Phase::Rearmed;
        c.phase = SlotRecord::Credit::Phase::Rearmed;
        c.due = due_after(now, bound_of(StorageRequest::Kind::Fetch));
        const ReadPass pass{
            d.req.slot,
            d.req.lba,
            d.req.block_count,
            d.block_size,
            d.block_size.extent(d.req.block_count),
            d.ack,
            backing
        };
        return answer_read_substitute_(pass, rearmed);
    }
    return d.req.write ? serve_write(d.req, d.block_size, d.ack, backing)
                       : serve_read(d.req, d.block_size, d.ack, backing);
}

Ex<SpiBlockPoll::Decode> BlockSlots::poll_block(
    IBlockRoles& roles,
    std::optional<SdStatusWord> known
) {
    return block_poll_.poll(roles, server_, known);
}

void BlockSlots::reset_all() noexcept {
    const os::RawMonotonicClock::time_point now = now_at_();
    for (SlotRecord& r : records_) {
        r.slot = Slot{};
        r.data.phase = SlotRecord::Credit::Phase::Rearmed;
        r.data.due = due_after(now, bound_of(StorageRequest::Kind::Fetch));
        r.ctl.phase = SlotRecord::Credit::Phase::Rearmed;
        r.ctl.due = due_after(now, bound_of(StorageRequest::Kind::Fetch));
    }
    for (SlotServer& sv : server_)
        sv = SlotServer::Session;
    lw_slots_ = BlockSlotMask{};
    invalidate_all();
}

}
