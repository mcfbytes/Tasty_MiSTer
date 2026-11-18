// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/file_tx_pieces.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "app/file_bytes.h"
#include "infra/path_parts.h"

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
    std::size_t got = 0;
    while (proto::FileSize{got} < n) {
        auto r = file_->read_at(
            sent_ + proto::FileSize{got},
            std::as_writable_bytes(std::span<std::uint8_t>(buf).subspan(got))
        );
        if (!r || *r == 0)
            return false;
        got += *r;
    }
    if (loader_ != nullptr) {
        loader_->shape(row_, buf, sent_);
        if (sent_ + n == proto::kFileStart + total_ && !loader_->finish({}))
            return false;
    }

    if (sent_ + n > keyed_to_) {
        key_.feed(
            std::span<const std::uint8_t>(buf).subspan((keyed_to_ - sent_).file_word<std::size_t>())
        );
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
    return ready(inbox, level);
}

Rung::Pass FileTxPieces::step(const RungEdges& edges, cores::ILadderHost& owner) {
    TASTY_SEAT_BODY(FileTxPieces);
    if (!ready(edges.inbox, edges.level))
        return Rung::Pass::Waiting;
    switch (step(edges.inbox, edges.level, owner.next_generation(), owner.link_width())) {
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

}
