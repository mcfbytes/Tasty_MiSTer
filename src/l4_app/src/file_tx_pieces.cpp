// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/file_tx_pieces.h"
#include "infra/span_cut.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "app/file_bytes.h"
#include "app/types.h"
#include "cores/progress.h"
#include "infra/path_parts.h"
#include "infra/prelude.h"

namespace mister::app {

namespace {

struct RestPlace {
    proto::FileSize whole{};
    proto::FileOffset offset{};

    [[nodiscard]] static RestPlace at_end(proto::FileSize whole, proto::FileOffset at) noexcept {
        return RestPlace{whole, at};
    }
};

enum Refusal : std::uint32_t { kBadTotal, kBadPath, kPastEnd };

}

Ex<FileTxPieces> FileTxPieces::start(
    std::unique_ptr<svc::IFile> file,
    proto::FileSize total,
    proto::WideIoIndex wire_index,
    std::string_view path,
    FileTxStamp stamp,
    std::unique_ptr<cores::ILoader> loader,
    const cores::TransferRow& row
) {
    constexpr proto::FileSize kMaxOpTotal{std::numeric_limits<std::uint32_t>::max()};
    if (file == nullptr || total.empty() || total > kMaxOpTotal) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), kBadTotal});
    }
    FileTxPieces p(std::move(file), total, wire_index, stamp, std::move(loader), row);
    if (!p.path_.assign(path))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), kBadPath});
    return p;
}

Ex<FileTxPieces> FileTxPieces::resume(
    std::unique_ptr<svc::IFile> file,
    proto::FileSize total,
    proto::WideIoIndex wire_index,
    std::string_view path,
    proto::FileOffset from,
    svc::Crc32 crc
) {
    if (from > proto::kFileStart + total)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), kPastEnd});
    auto p = start(std::move(file), total, wire_index, path);
    if (!p)
        return p;
    p->sent_ = from;
    p->keyed_to_ = proto::kFileStart + total;
    p->crc_ = crc;
    p->pieces_ = 1;
    p->facts_sent_ = false;
    return p;
}

std::optional<FileTxLevel> FileTxPieces::answer_(const FileTxLevelCell& level) const noexcept {
    if (!outstanding_)
        return std::nullopt;
    const auto s = level.sample();
    if (!s)
        return std::nullopt;

    if (!s.value.answers(outstanding_->act))
        return std::nullopt;
    return s.value;
}

bool FileTxPieces::ready(
    const LinkTxChannel<SeatTag::Main>& inbox,
    const FileTxLevelCell& level
) const noexcept {
    if (cut_owed_)
        return true;
    if (outstanding_ && !answer_(level))
        return false;
    if (inbox.ring().size() + 1 >= proto::kLinkTxCapacity)
        return false;

    return !(slab_full_at_ && inbox.ring().popped() == *slab_full_at_);
}

bool FileTxPieces::stage_(const proto::FileSize piece) {
    const proto::FileSize n = piece.min(sent_.remaining_in(total_));
    std::vector<std::uint8_t> buf = n.zeroed<std::vector<std::uint8_t>>();
    std::span<std::byte> left = std::as_writable_bytes(std::span<std::uint8_t>(buf));
    proto::FileOffset at = sent_;
    while (!left.empty()) {
        auto r = file_->read_at(at, left);
        if (!r || r->empty())
            return false;
        at += proto::FileSize::of(*r);
        left = r->past_in(left);
    }
    if (loader_ != nullptr) {
        loader_->shape(row_, buf, sent_);
        if (sent_ + n == proto::kFileStart + total_ && !loader_->finish({}))
            return false;
    }

    if (sent_ + n > keyed_to_) {
        key_.feed(infra::affine::from(std::span<const std::uint8_t>(buf), keyed_to_ - sent_));
        keyed_to_ = sent_ + n;
    }
    staged_ = FileIntern{.bytes = std::move(buf)};
    return true;
}

FileTxPieces::Pass FileTxPieces::step(
    LinkTxChannel<SeatTag::Main>& inbox,
    const FileTxLevelCell& level,
    proto::ActKey act,
    hal::Width width
) {
    TASTY_SEAT_BODY(FileTxPieces);
    using Phase = proto::LinkOp::FileTxPhase;
    if (cut_owed_)
        return cut_(inbox);
    if (!ready(inbox, level))
        return Pass::Waiting;

    if (outstanding_) {
        const auto ans = answer_(level);
        if (!ans)
            return Pass::Waiting;
        const Outstanding o = *outstanding_;
        outstanding_.reset();
        if (ans->held_act == o.act) {
            if (loader_ != nullptr || proto::FileSize{ans->held_bytes} > o.bytes)
                return fail_(inbox);
            sent_ = o.from + proto::FileSize{ans->held_bytes};
            ++rewinds_;
        } else if (!ans->stream_served) {
            return fail_(inbox);
        }
    }

    if (!width_)
        width_ = width;

    if (!staged_ && !stage_(piece_bytes(*width_)))
        return fail_(inbox);
    const proto::FileSize n = proto::FileSize::of(std::span<const std::uint8_t>(staged_->bytes));
    const bool last = sent_ + n == proto::kFileStart + total_;
    const Phase phase = pieces_ == 0 ? (last ? Phase::Whole : Phase::Open)
                                     : (last ? Phase::Close : Phase::Piece);

    if (last && !facts_sent_ && loader_ != nullptr && !loader_->facts().empty()) {

        const auto sent = inbox.push_load_facts(loader_->facts(), path_.view());
        if (!sent)
            return sent.error().code == Errc::would_block ? Pass::Waiting : fail_(inbox);
        facts_sent_ = true;
    }

    const RestPlace rest = n.empty() ? RestPlace::at_end(total_, sent_) : RestPlace{};
    const svc::Crc32 crc = last ? crc_.value_or(key_.value()) : svc::Crc32{};

    staged_->ext = infra::extension(path_.view());
    staged_->path = path_.view();
    staged_->crc = crc;
    staged_->piece = {.whole = rest.whole, .offset = rest.offset};

    if (last) {
        staged_->save = stamp_.save;
        staged_->edge = stamp_.edge;
    }

    auto id = inbox.intern_file(std::move(*staged_));
    if (!id) {
        if (id.error().code == Errc::would_block) {
            slab_full_at_ = inbox.ring().popped();
            return Pass::Waiting;
        }
        return fail_(inbox);
    }
    staged_.reset();

    if (last && facts_sent_)
        (void)inbox.stamp_remount(*id);
    const proto::LinkOp::FileTx op{
        .wire_index = wire_index_.fio_byte(),
        .phase = phase,
        .file = *id,
        .total = total_.file_word<std::uint32_t>(),
        .act = act
    };
    (void)inbox.push(op);
    slab_full_at_.reset();

    if (last) {
        last_ = LastOp{
            .act = act,
            .from = sent_,
            .crc = crc,
            .shaped = loader_ != nullptr,
            .stamped = stamp_.any(),
            .remounts_saves = facts_sent_
        };
    } else {
        outstanding_ = Outstanding{.act = act, .from = sent_, .bytes = n};
    }
    sent_ += n;
    ++pieces_;
    return last ? Pass::Done : Pass::Sent;
}

FileTxPieces::Pass FileTxPieces::fail_(LinkTxChannel<SeatTag::Main>& inbox) {
    if (pieces_ == 0)
        return Pass::Failed;
    cut_owed_ = true;
    return cut_(inbox);
}

FileTxPieces::Pass FileTxPieces::cut_(LinkTxChannel<SeatTag::Main>& inbox) {
    const proto::LinkOp::FileTx op{
        .wire_index = wire_index_.fio_byte(),
        .phase = proto::LinkOp::FileTxPhase::Close,
        .total = total_.file_word<std::uint32_t>()
    };
    if (!inbox.push(op))
        return Pass::Waiting;
    cut_owed_ = false;
    return Pass::Failed;
}

bool FileTxPieces::wants_pass(
    const LinkTxChannel<SeatTag::Main>& inbox,
    const FileTxLevelCell& level
) const noexcept {
    TASTY_SEAT_BODY(FileTxPieces);

    if (off_link_ && *off_link_)
        return !lane_waiting_;
    return ready(inbox, level);
}

Rung::Pass FileTxPieces::step(const RungEdges& edges, cores::ILadderHost& owner) {
    TASTY_SEAT_BODY(FileTxPieces);

    if (!off_link_)
        off_link_ = !resumed() && edges.lane && edges.lane->granted();
    const Pass pass = *off_link_ ? step_off_link_(edges, owner)
        : ready(edges.inbox, edges.level)
        ? step(edges.inbox, edges.level, owner.next_act(), owner.link_width())
        : Pass::Waiting;
    switch (pass) {
        case Pass::Sent:
            return Rung::Pass::Moved;
        case Pass::Waiting:
            return Rung::Pass::Waiting;
        case Pass::Done:
            return Rung::Pass::Done;
        case Pass::Failed:
            return Rung::Pass::Failed;
    }
    return Rung::Pass::Failed;
}

std::optional<bool> FileTxPieces::off_link_answer_(const FileTxLevelCell& level) const noexcept {
    const auto s = level.sample();
    if (!s || !s.value.answers(off_act_))
        return std::nullopt;
    return s.value.stream_served;
}

void FileTxPieces::off_link_progress_(const RungEdges& edges) noexcept {
    const cores::Progress now = cores::Progress::of_bytes(
        {.done = sent_ - proto::kFileStart, .total = total_}
    );
    if (now.done == bar_.done)
        return;
    bar_ = now;
    EventQueue<SeatTag::Main>::Producer events = edges.events;
    events.push(Event::ProgressUpdate{.progress = now}, Event::Head{ERR_SITE(), {}, kUncaused});
}

FileTxPieces::Pass FileTxPieces::step_off_link_(const RungEdges& edges, cores::ILadderHost& owner) {
    using Phase = proto::LinkOp::FileTxPhase;
    lane_waiting_ = false;
    OcramLaneWriter& lane = *edges.lane;
    const auto park = [this, &lane](OcramLaneWriter::Wait on) {
        lane.wait(on);
        lane_waiting_ = true;
        return Pass::Waiting;
    };
    const bool open = off_step_ != OffLink::Open && off_step_ != OffLink::Opening;

    if (open && lane.lost()) {
        cut_ended_ = true;
        off_step_ = OffLink::Cut;
    }
    switch (off_step_) {
        case OffLink::Open: {
            if (edges.inbox.ring().size() + 1 >= proto::kLinkTxCapacity)
                return Pass::Waiting;
            auto id = edges.inbox.intern_file_path(path_.view(), total_);
            if (!id)
                return id.error().code == Errc::would_block ? Pass::Waiting : Pass::Failed;
            if (!width_)
                width_ = owner.link_width();
            off_act_ = owner.next_act();
            (void)edges.inbox.push(
                proto::LinkOp::FileTx{
                    .wire_index = wire_index_.fio_byte(),
                    .phase = Phase::Open,
                    .file = *id,
                    .total = total_.file_word<std::uint32_t>(),
                    .act = off_act_
                }
            );
            off_step_ = OffLink::Opening;
            return Pass::Sent;
        }
        case OffLink::Opening: {
            const std::optional<bool> served = off_link_answer_(edges.level);
            if (!served)
                return Pass::Waiting;
            if (!*served)
                return Pass::Failed;
            ++pieces_;
            off_step_ = OffLink::Data;
            return off_link_data_(edges, lane);
        }
        case OffLink::Data:
            return off_link_data_(edges, lane);
        case OffLink::Acts:
        case OffLink::Before:
        case OffLink::Answering:
            return off_link_close_acts_(edges, owner);
        case OffLink::End:
            if (!lane.end())
                return park(OcramLaneWriter::Wait::Room);
            off_step_ = OffLink::Close;
            return Pass::Sent;
        case OffLink::Close: {
            if (!lane.played())
                return park(OcramLaneWriter::Wait::Played);

            lane.close_window();
            const svc::Crc32 crc = key_.value();
            auto id = edges.inbox.intern_file_path(path_.view(), total_, crc, 0, stamp_.edge);
            if (!id)
                return id.error().code == Errc::would_block ? Pass::Waiting : Pass::Failed;
            const proto::ActKey act = owner.next_act();
            if (!edges.inbox.push(
                    proto::LinkOp::FileTx{
                        .wire_index = wire_index_.fio_byte(),
                        .phase = Phase::Close,
                        .file = *id,
                        .total = total_.file_word<std::uint32_t>(),
                        .act = act
                    }
                )) {
                return Pass::Waiting;
            }
            last_ = LastOp{
                .act = act,
                .from = sent_,
                .crc = crc,
                .shaped = loader_ != nullptr,
                .stamped = stamp_.any(),
                .remounts_saves = facts_sent_
            };
            return Pass::Done;
        }
        case OffLink::Cut:
            return off_link_cut_(edges, lane);
    }
    return Pass::Failed;
}

FileTxPieces::Pass FileTxPieces::off_link_data_(const RungEdges& edges, OcramLaneWriter& lane) {

    if (!framed_) {
        if (!lane.open(edges.level.sample().value.params)) {
            lane.wait(OcramLaneWriter::Wait::Room);
            lane_waiting_ = true;
            return Pass::Waiting;
        }
        framed_ = true;
    }
    if (!staged_ && sent_ == proto::kFileStart + total_) {
        off_step_ = OffLink::Acts;
        return Pass::Sent;
    }
    if (!staged_) {
        if (!stage_(piece_bytes(*width_))) {
            off_step_ = OffLink::Cut;
            return off_link_cut_(edges, lane);
        }
        staged_at_ = proto::kFileStart;
    }
    const std::span<const std::uint8_t> piece(staged_->bytes);
    const proto::FileSize taken = lane.data(infra::affine::from(piece, staged_at_), *width_);
    if (taken.empty()) {
        lane.wait(OcramLaneWriter::Wait::Room);
        lane_waiting_ = true;
        return Pass::Waiting;
    }
    staged_at_ += taken;
    if (staged_at_ == proto::kFileStart + proto::FileSize::of(piece)) {
        sent_ += proto::FileSize::of(piece);
        staged_.reset();
        off_link_progress_(edges);
    }
    return Pass::Sent;
}

FileTxPieces::Pass FileTxPieces::off_link_close_acts_(
    const RungEdges& edges,
    cores::ILadderHost& owner
) {
    using Phase = proto::LinkOp::FileTxPhase;
    OcramLaneWriter& lane = *edges.lane;
    if (off_step_ == OffLink::Acts) {
        if (!lane.played()) {
            lane.wait(OcramLaneWriter::Wait::Played);
            lane_waiting_ = true;
            return Pass::Waiting;
        }
        if (!facts_sent_ && loader_ != nullptr && !loader_->facts().empty()) {
            const auto sent = edges.inbox.push_load_facts(loader_->facts(), path_.view());
            if (!sent)
                return sent.error().code == Errc::would_block ? Pass::Waiting : Pass::Failed;
            facts_sent_ = true;
        }
        off_step_ = stamp_.any() || facts_sent_ ? OffLink::Before : OffLink::End;
        return Pass::Sent;
    }
    if (off_step_ == OffLink::Before) {
        if (edges.inbox.ring().size() + 1 >= proto::kLinkTxCapacity)
            return Pass::Waiting;
        auto id = edges.inbox.intern_empty_file(
            FileIntern{.path = path_.view(), .save = stamp_.save, .edge = stamp_.edge}
        );
        if (!id)
            return id.error().code == Errc::would_block ? Pass::Waiting : Pass::Failed;
        if (facts_sent_)
            (void)edges.inbox.stamp_remount(*id);
        off_act_ = owner.next_act();
        (void)edges.inbox.push(
            proto::LinkOp::FileTx{
                .wire_index = wire_index_.fio_byte(),
                .phase = Phase::BeforeClose,
                .file = *id,
                .total = total_.file_word<std::uint32_t>(),
                .act = off_act_
            }
        );
        off_step_ = OffLink::Answering;
        return Pass::Sent;
    }
    const std::optional<bool> served = off_link_answer_(edges.level);
    if (!served)
        return Pass::Waiting;

    off_step_ = OffLink::End;
    return Pass::Sent;
}

FileTxPieces::Pass FileTxPieces::off_link_cut_(const RungEdges& edges, OcramLaneWriter& lane) {
    if (pieces_ == 0)
        return Pass::Failed;
    if (!cut_ended_) {
        if (framed_ && !lane.end()) {
            lane.wait(OcramLaneWriter::Wait::Room);
            lane_waiting_ = true;
            return Pass::Waiting;
        }
        cut_ended_ = true;
    }
    if (!lane.lost() && !lane.played()) {
        lane.wait(OcramLaneWriter::Wait::Played);
        lane_waiting_ = true;
        return Pass::Waiting;
    }
    lane.close_window();
    return cut_(edges.inbox);
}

}
