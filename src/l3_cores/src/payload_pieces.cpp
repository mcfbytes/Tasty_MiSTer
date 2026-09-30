// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/payload_pieces.h"

#include <algorithm>
#include <expected>
#include <span>
#include <utility>

namespace mister::cores {

namespace {

using Phase = proto::LinkOp::StagePhase;

bool fits(std::uint64_t size, std::uint16_t chunk) noexcept {
    return chunk != 0 || size <= PayloadPieces::kPieceBytes;
}

}  // namespace

Ex<PayloadPieces> PayloadPieces::of_file(std::unique_ptr<svc::IFile> file, std::uint64_t size,
                                         std::string_view ext, const Frame& frame) {
    if (file == nullptr || !fits(size, frame.chunk)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    return PayloadPieces(std::move(file), {}, size, ext, frame);
}

Ex<PayloadPieces> PayloadPieces::of_bytes(std::vector<std::uint8_t> bytes, std::string_view ext,
                                          const Frame& frame) {
    const std::uint64_t size = bytes.size();
    if (!fits(size, frame.chunk)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});
    }
    return PayloadPieces(nullptr, std::move(bytes), size, ext, frame);
}

bool PayloadPieces::stage_(std::size_t n) {
    std::vector<std::uint8_t> buf(n);
    if (file_ == nullptr) {
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(sent_), n, buf.begin());
    } else {
        std::size_t got = 0;
        while (got < n) {
            auto r = file_->read_at(
                sent_ + got, std::as_writable_bytes(std::span<std::uint8_t>(buf).subspan(got)));
            if (!r || *r == 0) return false;
            got += *r;
        }
    }
    staged_ = std::move(buf);
    return true;
}

PayloadPieces::Pass PayloadPieces::step(ILadderHost& host) {
    if (end_ != Pass::Waiting) return end_;
    if (cut_owed_) return fail_(host);
    if (word_bytes_ == 0) word_bytes_ = host.link_word_bytes();
    while (host.link_in_flight() < kAhead) {
        const std::uint64_t left = size_ - sent_;
        const auto n = static_cast<std::size_t>(
            std::min<std::uint64_t>(left, piece_bytes(frame_.chunk, word_bytes_)));
        const bool first = sent_ == 0;
        const bool last = n == left;

        if (!staged_ && !stage_(n)) return fail_(host);

        proto::FileId id{};
        if (n != 0) {
            auto r = host.intern_piece(
                *staged_, first ? std::string_view{ext_} : std::string_view{}, size_, sent_);
            if (!r) return r.error().code == Errc::would_block ? Pass::Waiting : fail_(host);
            id = *r;
        }
        proto::LinkOp::StagePayload op{.payload = id,
                                       .dest = frame_.dest,
                                       .progress = frame_.progress,
                                       .phase = first ? (last ? Phase::Whole : Phase::Open)
                                                      : (last ? Phase::Close : Phase::Piece),
                                       .chunk = frame_.chunk};
        if (last) {
            op.region = frame_.region;
            op.act = frame_.act;
        }
        if (!host.order(op)) return Pass::Waiting;
        staged_.reset();
        sent_ += n;
        if (last) return end_ = Pass::Done;
    }
    return Pass::Waiting;
}

PayloadPieces::Pass PayloadPieces::fail_(ILadderHost& host) {
    if (sent_ == 0) return end_ = Pass::Failed;
    cut_owed_ = true;
    const proto::LinkOp::StagePayload cut{
        .dest = frame_.dest, .phase = Phase::Close, .chunk = frame_.chunk};
    if (!host.order(cut)) return Pass::Waiting;
    cut_owed_ = false;
    return end_ = Pass::Failed;
}

}  // namespace mister::cores
