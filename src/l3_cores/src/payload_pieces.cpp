// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/payload_pieces.h"

#include <algorithm>
#include <expected>
#include <span>
#include <utility>

namespace mister::cores {

namespace {

using Phase = proto::LinkOp::StagePhase;

bool fits(proto::FileSize size, WireChunk chunk) noexcept {
    return !chunk.one_write() || size <= proto::FileSize{PayloadPieces::kPieceBytes};
}

}

Ex<PayloadPieces> PayloadPieces::of_file(
    std::unique_ptr<svc::IFile> file,
    proto::FileSize size,
    std::string_view ext,
    const Frame& frame
) {
    if (file == nullptr || !fits(size, frame.chunk)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    return PayloadPieces(std::move(file), {}, size, ext, frame);
}

Ex<PayloadPieces> PayloadPieces::of_bytes(
    std::vector<std::uint8_t> bytes,
    std::string_view ext,
    const Frame& frame
) {
    const proto::FileSize size = proto::FileSize{bytes.size()};
    if (!fits(size, frame.chunk)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});
    }
    return PayloadPieces(nullptr, std::move(bytes), size, ext, frame);
}

Ex<PayloadPieces> PayloadPieces::held_window(
    std::vector<std::uint8_t> bytes,
    const HeldFrame& frame
) {
    const proto::FileSize size = proto::FileSize{bytes.size()};

    if (frame.piece == proto::FioWordCount{}
        || frame.piece > proto::FioWordCount{proto::tightest_load_budget_words()}) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    PayloadPieces run(nullptr, std::move(bytes), size, {}, Frame{});
    run.held_ = frame;
    return run;
}

proto::LinkOp PayloadPieces::piece_op_(proto::FileId id, proto::LinkOp::StagePhase phase) {
    if (held_) {
        return infra::make<proto::LinkOp>(proto::LinkOp::PlainDownload{
            .payload = id,
            .dest = held_->dest,
            .phase = phase,
            .framing = held_->framing
        });
    }
    proto::LinkOp::StagePayload
        op{.payload = id, .dest = frame_.dest, .progress = frame_.progress, .phase = phase};
    frame_.chunk.stamp(op);
    const bool last = phase == proto::LinkOp::StagePhase::Whole
        || phase == proto::LinkOp::StagePhase::Close;
    if (last) {
        op.region = frame_.region;
        op.act = frame_.act;
    }
    return infra::make<proto::LinkOp>(op);
}

Ex<void> PayloadPieces::stage_(std::size_t n) {
    std::vector<std::uint8_t> buf(n);
    if (file_ == nullptr) {
        std::copy_n(sent_.skip_in(std::span<const std::uint8_t>(bytes_)).begin(), n, buf.begin());
    } else {
        std::size_t got = 0;
        while (got < n) {
            auto r = file_->read_at(
                sent_ + proto::FileSize{got},
                std::as_writable_bytes(std::span<std::uint8_t>(buf).subspan(got))
            );
            if (!r)
                return std::unexpected(r.error());
            if (*r == 0)
                return std::unexpected(Error{Errc::short_read, ERR_SITE(), 0});
            got += *r;
        }
    }
    staged_ = std::move(buf);
    return {};
}

Ex<PayloadPieces::Pass> PayloadPieces::step(ILadderHost& host) {
    if (failure_) {
        if (cut_owed_)
            return fail_(host);
        return std::unexpected(*failure_);
    }
    if (end_ != Pass::Waiting)
        return end_;
    if (!width_)
        width_ = host.link_width();
    while (host.link_in_flight() < kAhead) {
        const proto::FileSize left = sent_.remaining_in(size_);

        const std::size_t n = left.clamp_write(
            proto::kFileStart,
            held_ ? held_->piece.bytes_at(held_->framing) : piece_bytes(frame_.chunk, *width_)
        );
        const bool first = sent_ == proto::kFileStart;
        const bool last = proto::FileSize{n} == left;

        if (!staged_) {
            auto read = stage_(n);
            if (!read) {
                failure_ = std::move(read.error());
                return fail_(host);
            }
        }

        proto::FileId id{};
        if (n != 0) {
            auto r = host.intern_piece(
                *staged_,
                first ? std::string_view{ext_} : std::string_view{},
                size_,
                sent_
            );
            if (!r) {
                if (r.error().code == Errc::would_block)
                    return Pass::Waiting;
                failure_ = r.error();
                return fail_(host);
            }
            id = *r;
        }
        const proto::LinkOp op = piece_op_(
            id,
            first ? (last ? Phase::Whole : Phase::Open) : (last ? Phase::Close : Phase::Piece)
        );
        if (!host.order(op))
            return Pass::Waiting;
        staged_.reset();
        sent_ += proto::FileSize{n};
        if (last)
            return end_ = Pass::Done;
    }
    return Pass::Waiting;
}

Ex<PayloadPieces::Pass> PayloadPieces::fail_(ILadderHost& host) {
    if (sent_ == proto::kFileStart)
        return std::unexpected(*failure_);
    cut_owed_ = true;
    const proto::LinkOp cut = piece_op_(proto::FileId{}, Phase::Close);
    if (!host.order(cut))
        return Pass::Waiting;
    cut_owed_ = false;
    return std::unexpected(*failure_);
}

}
