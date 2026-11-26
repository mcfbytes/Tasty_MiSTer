// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/payload_pieces.h"
#include "infra/prelude.h"
#include "infra/span_cut.h"

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
        || frame.piece > proto::FioWordCount{proto::link::tightest_load_budget_words()}) {
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

Ex<void> PayloadPieces::stage_(proto::FileSize n) {
    auto buf = n.zeroed<std::vector<std::uint8_t>>();
    if (file_ == nullptr) {
        std::copy_n(
            infra::affine::from(std::span<const std::uint8_t>(bytes_), sent_).begin(),
            buf.size(),
            buf.begin()
        );
    } else {
        if (auto r = file_->read_exact_at(
                sent_,
                std::as_writable_bytes(std::span<std::uint8_t>(buf))
            );
            !r) {
            return std::unexpected(r.error());
        }
    }
    staged_ = std::move(buf);
    return {};
}

Ex<PayloadPieces::Pass> PayloadPieces::step(ILadderHost& host) {

    if (!off_link_)
        off_link_ = !held_ && !size_.empty() && host.off_link();
    if (*off_link_)
        return step_off_link_(host);
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

        const proto::FileSize n = left.min(
            proto::FileSize{
                held_ ? held_->piece.bytes_at(held_->framing) : piece_bytes(frame_.chunk, *width_)
            }
        );
        const bool first = sent_ == proto::kFileStart;
        const bool last = n == left;

        if (!staged_) {
            auto read = stage_(n);
            if (!read) {
                failure_ = std::move(read.error());
                return fail_(host);
            }
        }

        proto::FileId id{};
        if (!n.empty()) {
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
        sent_ += n;
        if (last)
            return end_ = Pass::Done;
    }
    return Pass::Waiting;
}

Ex<PayloadPieces::Pass> PayloadPieces::order_off_link_(
    ILadderHost& host,
    Phase phase,
    bool names_bytes
) {
    proto::FileId id{};
    if (names_bytes) {

        const std::string_view ext = phase == Phase::Open ? std::string_view{ext_}
                                                          : std::string_view{};
        auto r = host.intern_piece({}, ext, size_, proto::kFileStart + size_);
        if (!r) {
            if (r.error().code == Errc::would_block)
                return Pass::Waiting;
            return std::unexpected(r.error());
        }
        id = *r;
    }
    proto::ActKey16 key = host.next_act().narrow();
    if (!key)
        key = host.next_act().narrow();
    proto::LinkOp::StagePayload op{.payload = id, .dest = frame_.dest, .phase = phase};
    if (phase == Phase::Close)
        op.region = frame_.region;
    op.act = key;
    if (!host.order(op))
        return Pass::Waiting;
    off_act_ = key;
    off_step_ = phase == Phase::Open ? OffLinkStep::Opening : OffLinkStep::Closing;
    return Pass::Waiting;
}

Ex<PayloadPieces::Pass> PayloadPieces::step_off_link_(ILadderHost& host) {
    if (end_ != Pass::Waiting)
        return end_;
    if (!width_)
        width_ = host.link_width();
    for (;;) {
        switch (off_step_) {
            case OffLinkStep::Open:
                return order_off_link_(host, Phase::Open, true);
            case OffLinkStep::Opening:
                switch (host.payload_answer(off_act_)) {
                    case FileTxAnswer::Pending:
                        return Pass::Waiting;
                    case FileTxAnswer::Dropped:
                        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
                    case FileTxAnswer::Served:
                        off_step_ = OffLinkStep::Data;
                        break;
                }
                break;
            case OffLinkStep::Data: {
                if (!staged_ && sent_ == proto::kFileStart + size_) {
                    off_step_ = OffLinkStep::Ending;
                    break;
                }
                if (!staged_) {
                    const proto::FileSize n = sent_.remaining_in(size_).min(
                        proto::FileSize{piece_bytes(frame_.chunk, *width_)}
                    );
                    if (auto read = stage_(n); !read) {
                        failure_ = std::move(read.error());
                        off_step_ = OffLinkStep::Ending;
                        break;
                    }
                    staged_at_ = proto::kFileStart;
                }
                const std::span<const std::uint8_t> piece(*staged_);
                auto taken = host.send_off_link(infra::affine::from(piece, staged_at_));
                if (!taken) {
                    failure_ = taken.error();
                    off_step_ = OffLinkStep::Ending;
                    break;
                }
                if (taken->empty())
                    return Pass::Waiting;
                staged_at_ += *taken;
                if (staged_at_ == proto::kFileStart + proto::FileSize::of(piece)) {
                    sent_ += proto::FileSize::of(piece);
                    staged_.reset();
                }
                break;
            }
            case OffLinkStep::Ending:
                if (!host.end_off_link())
                    return Pass::Waiting;
                return order_off_link_(host, Phase::Close, !failure_.has_value());
            case OffLinkStep::Closing:
                switch (host.payload_answer(off_act_)) {
                    case FileTxAnswer::Pending:
                        return Pass::Waiting;
                    case FileTxAnswer::Dropped: {
                        const Error dropped{Errc::negotiation, ERR_SITE(), 0};
                        return std::unexpected(failure_.value_or(dropped));
                    }
                    case FileTxAnswer::Served:
                        if (failure_)
                            return std::unexpected(*failure_);
                        return end_ = Pass::Done;
                }
                return Pass::Waiting;
        }
    }
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
