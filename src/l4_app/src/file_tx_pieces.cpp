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

}  // namespace

Ex<FileTxPieces> FileTxPieces::start(std::unique_ptr<svc::IFile> file, std::uint64_t total,
                                     std::uint8_t wire_index, std::string_view path,
                                     std::unique_ptr<cores::ILoader> loader,
                                     const cores::TransferRow& row) {
    if (file == nullptr || total == 0 || total > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    FileTxPieces p(std::move(file), total, wire_index, std::move(loader), row);
    if (!p.path_.assign(path)) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});
    return p;
}

bool FileTxPieces::ready(const LinkTxChannel& inbox) const noexcept {
    if (inbox.ring().size() + 1 >= proto::kLinkTxCapacity) return false;

    if (slab_full_at_ && inbox.ring().popped() == *slab_full_at_) return false;
    if (cut_owed_ || pieces_ < kAhead) return true;

    return static_cast<std::int32_t>(inbox.ring().popped() - prev_at_) > 0;
}

bool FileTxPieces::stage_() {
    const auto n =
        static_cast<std::size_t>(std::min<std::uint64_t>(FileBytes::kPieceBytes, total_ - sent_));
    std::vector<std::uint8_t> buf(n);
    std::size_t got = 0;
    while (got < n) {
        auto r = file_->read_at(sent_ + got,
                                std::as_writable_bytes(std::span<std::uint8_t>(buf).subspan(got)));
        if (!r || *r == 0) return false;
        got += *r;
    }
    if (loader_ != nullptr) {
        loader_->shape(row_, buf, sent_);
        if (sent_ + n == total_ && !loader_->finish(nullptr)) return false;
    }
    key_.feed(buf);
    staged_ = std::move(buf);
    return true;
}

FileTxPieces::Pass FileTxPieces::step(LinkTxChannel& inbox) {
    using Phase = proto::LinkOp::FileTxPhase;
    if (cut_owed_) return cut_(inbox);
    if (!ready(inbox)) return Pass::Waiting;

    if (!staged_ && !stage_()) return fail_(inbox);
    const std::uint64_t n = staged_->size();
    const bool last = sent_ + n == total_;
    const Phase phase =
        pieces_ == 0 ? (last ? Phase::Whole : Phase::Open) : (last ? Phase::Close : Phase::Piece);

    if (last && !facts_sent_ && loader_ != nullptr && !loader_->facts().empty()) {
        auto f = inbox.intern(loader_->facts());
        if (!f) return f.error().code == Errc::would_block ? Pass::Waiting : fail_(inbox);
        (void)inbox.push(proto::LinkOp::LoadFacts{.facts = *f});
        facts_sent_ = true;
    }

    const std::uint32_t at = inbox.ring().pushed();
    auto id = inbox.intern_file(std::move(*staged_), ext_of(path_.view()), path_.view(), 0,
                                last ? key_.value() : 0);
    if (!id) {
        if (id.error().code == Errc::would_block) {
            slab_full_at_ = inbox.ring().popped();
            return Pass::Waiting;
        }
        return fail_(inbox);
    }
    staged_.reset();
    const proto::LinkOp::FileTx op{.wire_index = wire_index_,
                                   .phase = phase,
                                   .file = *id,
                                   .total = static_cast<std::uint32_t>(total_)};
    (void)inbox.push(op);
    slab_full_at_.reset();
    sent_ += n;
    prev_at_ = last_at_;
    last_at_ = at;
    ++pieces_;
    return last ? Pass::Done : Pass::Sent;
}

FileTxPieces::Pass FileTxPieces::fail_(LinkTxChannel& inbox) {
    if (pieces_ == 0) return Pass::Failed;
    cut_owed_ = true;
    return cut_(inbox);
}

FileTxPieces::Pass FileTxPieces::cut_(LinkTxChannel& inbox) {
    const proto::LinkOp::FileTx op{.wire_index = wire_index_,
                                   .phase = proto::LinkOp::FileTxPhase::Close,
                                   .total = static_cast<std::uint32_t>(total_)};
    if (!inbox.push(op)) return Pass::Waiting;
    cut_owed_ = false;
    return Pass::Failed;
}

}  // namespace mister::app
