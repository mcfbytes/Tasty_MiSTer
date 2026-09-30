// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/load_ladder.h"

#include <algorithm>
#include <limits>
#include <span>
#include <vector>

#include "app/file_bytes.h"
#include "cores/progress_ticker.h"
#include "infra/message_sum.h"

namespace mister::app {

Ex<LoadLadder> LoadLadder::start(Job job, LoadWindow window, std::string_view path) {
    const cores::TransferRow& r = job.row;
    const std::uint64_t placed = job.total * (r.stride == 0 ? 1u : r.stride);
    const std::uint64_t extent = r.extent != 0 ? r.extent : placed;

    if (job.loader == nullptr || (job.file == nullptr && job.total != 0) || extent == 0 ||
        r.stride == 0 || job.total > std::numeric_limits<std::uint32_t>::max() || placed > extent) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    if (job.mirror && (r.stride != 1 || r.placed || job.mirror->size() < extent)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 2});
    }
    LoadLadder l(std::move(job), std::move(window));
    if (!l.path_.assign(path)) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});
    l.extent_ = extent;
    l.buf_.resize(static_cast<std::size_t>(std::min<std::uint64_t>(
        FileBytes::kPieceBytes, std::max<std::uint64_t>(l.job_.total, extent - placed))));
    return l;
}

bool LoadLadder::wants_pass(const LinkTxChannel& inbox,
                            const FileTxLevelCell* level) const noexcept {
    if (pc_ == Pc::Over) return false;
    if (pc_ == Pc::AwaitOpen) {
        if (level == nullptr) return false;
        const auto s = level->sample();
        return s && s.value.act == job_.act && !s.value.payload;
    }
    if (pc_ == Pc::Chunk || pc_ == Pc::Fill || pc_ == Pc::Conclude || pc_ == Pc::Finish) {
        return true;
    }
    return inbox.ring().size() < proto::kLinkTxCapacity;
}

LoadLadder::Pass LoadLadder::step(Host& h) {
    switch (pc_) {
        case Pc::Open:
            return open_(h);
        case Pc::AwaitOpen:
            return await_open_(h);
        case Pc::Chunk:
            return chunk_(h);
        case Pc::Fill:
            return fill_(h);
        case Pc::Conclude:
            return conclude_(h);
        case Pc::Finish:
            return finish_();
        case Pc::Facts:
            return facts_(h);
        case Pc::Close:
            return close_(h);
        case Pc::Cut:
            return cut_(h);
        case Pc::Over:
            break;
    }
    return Pass::Failed;
}

LoadLadder::Pass LoadLadder::open_(Host& h) {

    if (job_.row.bracket == cores::RowBracket::None) {
        progress_(h, true);
        pc_ = job_.total != 0 ? Pc::Chunk : Pc::Fill;
        return Pass::Stepped;
    }
    auto id = h.inbox.intern_file_path(path_.view(), job_.total);
    if (!id) return id.error().code == Errc::would_block ? Pass::Waiting : Pass::Failed;
    const proto::LinkOp::FileTx op{.wire_index = job_.wire_index,
                                   .phase = proto::LinkOp::FileTxPhase::WindowOpen,
                                   .file = *id,
                                   .total = static_cast<std::uint32_t>(extent_),
                                   .act = job_.act};
    if (!h.inbox.push(op)) return Pass::Waiting;
    progress_(h, true);
    pc_ = Pc::AwaitOpen;
    return Pass::Stepped;
}

LoadLadder::Pass LoadLadder::await_open_(Host& h) {
    if (h.level == nullptr) return Pass::Waiting;
    const auto s = h.level->sample();
    if (!s || s.value.act != job_.act || s.value.payload) return Pass::Waiting;
    if (!s.value.open) {
        pc_ = Pc::Over;
        progress_(h, true);
        return Pass::Failed;
    }
    pc_ = Pc::Chunk;
    return Pass::Stepped;
}

LoadLadder::Pass LoadLadder::chunk_(Host& h) {
    const cores::TransferRow& row = job_.row;

    const std::size_t cap = std::min(buf_.size(), FileBytes::kPieceBytes / row.stride);
    const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(cap, job_.total - off_));
    const std::span<std::uint8_t> piece(buf_.data(), n);
    std::size_t got = 0;
    while (got < n) {
        auto r = job_.file->read_at(row.file_offset + off_ + got,
                                    std::as_writable_bytes(piece.subspan(got)));
        if (!r || *r == 0) return cut_(h);
        got += *r;
    }
    if (row.shaped) job_.loader->shape(row, piece, off_);
    if (row.placed) {
        if (!job_.loader->place(row, piece, off_, window_)) return cut_(h);
    } else {
        const auto placed =
            row.stride == 1
                ? window_.write(static_cast<std::size_t>(off_), std::as_bytes(piece))
                : window_.write_lanes(static_cast<std::size_t>(off_ * row.stride + 2u * row.lane),
                                      std::as_bytes(piece), row.stride);
        if (!placed) return cut_(h);
        if (job_.mirror &&
            !job_.mirror->write(static_cast<std::size_t>(off_), std::as_bytes(piece))) {
            return cut_(h);
        }
    }
    off_ += n;
    progress_(h, false);
    if (off_ == job_.total) {
        if (row.placed) {
            pc_ = Pc::Conclude;
        } else {
            pc_ = extent_ > off_ * row.stride ? Pc::Fill : Pc::Finish;
        }
    }
    return Pass::Stepped;
}

LoadLadder::Pass LoadLadder::fill_(Host& h) {
    const std::uint64_t from = job_.total * job_.row.stride;
    if (fill_off_ < from) fill_off_ = from;
    if (fill_off_ >= extent_) {
        pc_ = Pc::Finish;
        return Pass::Stepped;
    }
    const auto n =
        static_cast<std::size_t>(std::min<std::uint64_t>(buf_.size(), extent_ - fill_off_));
    std::fill_n(buf_.begin(), n, job_.row.fill);
    const std::span<const std::uint8_t> tail(buf_.data(), n);
    if (!window_.write(static_cast<std::size_t>(fill_off_), std::as_bytes(tail))) return cut_(h);
    if (job_.mirror &&
        !job_.mirror->write(static_cast<std::size_t>(fill_off_), std::as_bytes(tail))) {
        return cut_(h);
    }
    fill_off_ += n;
    return Pass::Stepped;
}

LoadLadder::Pass LoadLadder::conclude_(Host& h) {
    auto more = job_.loader->conclude(job_.row, window_);
    if (!more) return cut_(h);
    if (!*more) pc_ = Pc::Finish;
    return Pass::Stepped;
}

LoadLadder::Pass LoadLadder::finish_() {
    if (!job_.loader->finish(&window_)) {
        pc_ = Pc::Cut;
        return Pass::Stepped;
    }

    window_.handoff();
    if (job_.mirror) job_.mirror->handoff();
    pc_ = Pc::Facts;
    return Pass::Stepped;
}

LoadLadder::Pass LoadLadder::facts_(Host& h) {
    const std::span<const std::uint8_t> facts = job_.loader->facts();
    if (!facts.empty()) {
        if (h.inbox.ring().size() >= proto::kLinkTxCapacity) return Pass::Waiting;
        auto id = h.inbox.intern(facts);
        if (!id) return id.error().code == Errc::would_block ? Pass::Waiting : cut_(h);
        if (!h.inbox.push(proto::LinkOp::LoadFacts{.facts = *id})) return Pass::Waiting;
    }
    pc_ = Pc::Close;
    return Pass::Stepped;
}

LoadLadder::Pass LoadLadder::close_(Host& h) {
    if (job_.row.bracket == cores::RowBracket::None) return notify_(h);
    auto id = h.inbox.intern_file_path(path_.view(), job_.total);
    if (!id) return id.error().code == Errc::would_block ? Pass::Waiting : cut_(h);
    const proto::LinkOp::FileTx op{.wire_index = job_.wire_index,
                                   .phase = proto::LinkOp::FileTxPhase::WindowClose,
                                   .file = *id,
                                   .total = static_cast<std::uint32_t>(extent_)};
    if (!h.inbox.push(op)) return Pass::Waiting;
    pc_ = Pc::Over;
    progress_(h, true);
    return Pass::Done;
}

LoadLadder::Pass LoadLadder::notify_(Host& h) {
    const cores::PostNotify& n = job_.row.notify;
    if (n.armed) {
        if (h.inbox.ring().size() >= proto::kLinkTxCapacity) return Pass::Waiting;
        std::vector<std::uint8_t> words;
        words.reserve(2u * n.words.size());
        for (const std::uint16_t w : n.words) {
            words.push_back(static_cast<std::uint8_t>(w & 0xFFu));
            words.push_back(static_cast<std::uint8_t>(w >> 8));
        }
        auto id = h.inbox.intern_file(std::move(words), {}, path_.view(), 0);
        if (!id) return id.error().code == Errc::would_block ? Pass::Waiting : cut_(h);
        const proto::LinkOp::StagePayload op{.payload = *id,
                                             .dest = proto::WideIoIndex{cores::PostNotify::kIndex},
                                             .copy_word = n.copies() ? cores::PostNotify::kCopyWord
                                                                     : std::uint8_t{0}};
        if (!h.inbox.push(op)) return Pass::Waiting;
    }
    pc_ = Pc::Over;
    progress_(h, true);
    return Pass::Done;
}

LoadLadder::Pass LoadLadder::cut_(Host& h) {
    pc_ = Pc::Cut;
    if (job_.row.bracket == cores::RowBracket::None) {
        pc_ = Pc::Over;
        progress_(h, true);
        return Pass::Failed;
    }
    const proto::LinkOp::FileTx op{.wire_index = job_.wire_index,
                                   .phase = proto::LinkOp::FileTxPhase::WindowClose,
                                   .total = static_cast<std::uint32_t>(job_.total)};
    if (!h.inbox.push(op)) return Pass::Waiting;
    pc_ = Pc::Over;
    progress_(h, true);
    return Pass::Failed;
}

void LoadLadder::progress_(Host& h, bool edge) noexcept {
    std::uint16_t cur = 0;
    std::uint16_t max = 0;
    if (!edge) {
        max = cores::kProgressSteps;
        cur = static_cast<std::uint16_t>(off_ * cores::kProgressSteps / job_.total);
    }
    (void)h.events.push(infra::make<Event>(Event::ProgressUpdate{.cur = cur, .max = max},
                                           Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}));
}

}  // namespace mister::app
