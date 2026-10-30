// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/file_tx_pieces.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "app/file_bytes.h"

namespace mister::app {

namespace {

std::string_view ext_of(std::string_view path) noexcept {
    const std::size_t dot = path.rfind('.');
    return dot == std::string_view::npos ? std::string_view{} : path.substr(dot);
}

}

Ex<FileTxPieces> FileTxPieces::start(
    std::unique_ptr<svc::IFile> file,
    std::uint64_t total,
    proto::WideIoIndex wire_index,
    std::string_view path,
    std::unique_ptr<cores::ILoader> loader,
    const cores::TransferRow& row
) {
    if (file == nullptr || total == 0 || total > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    FileTxPieces p(std::move(file), total, wire_index, std::move(loader), row);
    if (!p.path_.assign(path))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});
    return p;
}

Ex<FileTxPieces> FileTxPieces::resume(
    std::unique_ptr<svc::IFile> file,
    std::uint64_t total,
    proto::WideIoIndex wire_index,
    std::string_view path,
    std::uint64_t from,
    std::uint32_t crc
) {
    if (from > total)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 2});
    auto p = start(std::move(file), total, wire_index, path);
    if (!p)
        return p;
    p->sent_ = from;
    p->keyed_to_ = total;
    p->crc_ = crc;
    p->pieces_ = 1;
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

bool FileTxPieces::stage_() {
    const auto n = static_cast<std::size_t>(
        std::min<std::uint64_t>(InternedFile::kPieceBytes, total_ - sent_)
    );
    std::vector<std::uint8_t> buf(n);
    std::size_t got = 0;
    while (got < n) {
        auto r = file_->read_at(
            sent_ + got,
            std::as_writable_bytes(std::span<std::uint8_t>(buf).subspan(got))
        );
        if (!r || *r == 0)
            return false;
        got += *r;
    }
    if (loader_ != nullptr) {
        loader_->shape(row_, buf, sent_);
        if (sent_ + n == total_ && !loader_->finish(nullptr))
            return false;
    }

    if (sent_ + n > keyed_to_) {
        key_.feed(
            std::span<const std::uint8_t>(buf).subspan(static_cast<std::size_t>(keyed_to_ - sent_))
        );
        keyed_to_ = sent_ + n;
    }
    staged_ = std::move(buf);
    return true;
}

FileTxPieces::Pass FileTxPieces::step(
    LinkTxChannel<SeatTag::Main>& inbox,
    const FileTxLevelCell& level,
    std::uint32_t act
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
            if (loader_ != nullptr || ans->held_bytes > o.bytes)
                return fail_(inbox);
            sent_ = o.from + ans->held_bytes;
            ++rewinds_;
        } else if (!ans->stream_served) {
            return fail_(inbox);
        }
    }

    if (!staged_ && !stage_())
        return fail_(inbox);
    const std::uint64_t n = staged_->size();
    const bool last = sent_ + n == total_;
    const Phase phase = pieces_ == 0 ? (last ? Phase::Whole : Phase::Open)
                                     : (last ? Phase::Close : Phase::Piece);

    if (last && !facts_sent_ && loader_ != nullptr && !loader_->facts().empty()) {
        auto f = inbox.intern(loader_->facts());
        if (!f)
            return f.error().code == Errc::would_block ? Pass::Waiting : fail_(inbox);
        (void)inbox.push(proto::LinkOp::LoadFacts{.facts = *f});
        facts_sent_ = true;
    }

    const std::uint64_t whole = n == 0 ? total_ : 0;
    const std::uint64_t offset = n == 0 ? sent_ : 0;
    const std::uint32_t crc = last ? crc_.value_or(key_.value()) : 0;
    auto id = inbox.intern_file(
        std::move(*staged_),
        ext_of(path_.view()),
        path_.view(),
        0,
        crc,
        whole,
        offset
    );
    if (!id) {
        if (id.error().code == Errc::would_block) {
            slab_full_at_ = inbox.ring().popped();
            return Pass::Waiting;
        }
        return fail_(inbox);
    }
    staged_.reset();
    const proto::LinkOp::FileTx op{
        .wire_index = wire_index_.fio_byte(),
        .phase = phase,
        .file = *id,
        .total = static_cast<std::uint32_t>(total_),
        .act = act
    };
    (void)inbox.push(op);
    slab_full_at_.reset();

    if (last) {
        last_ = LastOp{.act = act, .from = sent_, .crc = crc, .shaped = loader_ != nullptr};
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
        .total = static_cast<std::uint32_t>(total_)
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
    switch (step(edges.inbox, edges.level, owner.next_generation())) {
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
