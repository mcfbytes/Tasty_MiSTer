// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/load_ladder.h"
#include "infra/span_cut.h"

#include "infra/endian.h"

#include <algorithm>
#include <limits>
#include <span>
#include <vector>

#include "app/file_bytes.h"
#include "cores/progress_ticker.h"
#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"

namespace mister::app {

Ex<LoadLadder> LoadLadder::start(
    Job job,
    LoadWindow<seat_owner::Main> window,
    std::string_view path
) {
    const cores::TransferRow& r = job.row;
    const std::uint64_t placed = r.spread_bytes(proto::FileSize{job.total})
                                     .file_word<std::size_t>();
    const std::uint64_t extent = r.extent_bytes(proto::FileSize{job.total})
                                     .file_word<std::size_t>();

    if (job.loader == nullptr
        || (job.file == nullptr && job.total != 0)
        || extent == 0
        || r.stride == 0
        || job.total > std::numeric_limits<std::uint32_t>::max()
        || placed > extent) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    if (job.mirror && (r.stride != 1 || r.placed || job.mirror->size() < extent)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 2});
    }
    LoadLadder l(std::move(job), std::move(window));
    if (!l.path_.assign(path))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});
    l.extent_ = extent;
    l.buf_.resize(
        static_cast<std::size_t>(std::min<std::uint64_t>(
            InternedFile::kPieceBytes,
            std::max<std::uint64_t>(l.job_.total, extent - placed)
        ))
    );
    return l;
}

bool LoadLadder::wants_pass(
    const LinkTxChannel<SeatTag::Main>& inbox,
    const FileTxLevelCell& level
) const noexcept {
    TASTY_SEAT_BODY(LoadLadder);
    switch (pc_) {
        case Pc::Over:
            return false;
        case Pc::AwaitOpen: {
            const auto s = level.sample();
            return s && s.value.act == job_.act && !s.value.payload;
        }
        case Pc::Chunk:
        case Pc::Fill:
        case Pc::Conclude:
        case Pc::Finish:
            return true;
        case Pc::Open:
        case Pc::Facts:
        case Pc::Mount:
        case Pc::Close:
        case Pc::Cut:
            return inbox.ring().size() < proto::kLinkTxCapacity;
    }

    return false;
}

LoadLadder::Pass LoadLadder::step(Host& h) {
    TASTY_SEAT_BODY(LoadLadder);
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
        case Pc::Mount:
            return mount_(h);
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
    auto id = h.inbox.intern_file_path(path_.view(), proto::FileSize{job_.total}, {}, 0, job_.edge);
    if (!id)
        return id.error().code == Errc::would_block ? Pass::Waiting : Pass::Failed;
    const proto::LinkOp::FileTx op{
        .wire_index = job_.wire_index.fio_byte(),
        .phase = proto::LinkOp::FileTxPhase::WindowOpen,
        .file = *id,
        .total = bracket_length_().detail(),
        .act = job_.act
    };
    if (!h.inbox.push(op))
        return Pass::Waiting;
    progress_(h, true);
    pc_ = Pc::AwaitOpen;
    return Pass::Stepped;
}

LoadLadder::Pass LoadLadder::await_open_(Host& h) {
    const auto s = h.level.sample();
    if (!s || s.value.act != job_.act || s.value.payload)
        return Pass::Waiting;
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

    const std::size_t cap = std::min(buf_.size(), InternedFile::kPieceBytes / row.stride);
    const proto::FileSize chunk = (proto::FileSize{job_.total} - off_).min(proto::FileSize{cap});
    const std::span<std::uint8_t> piece = infra::affine::first(
        std::span<std::uint8_t>{buf_},
        chunk
    );
    std::span<std::byte> left = std::as_writable_bytes(piece);
    proto::FileOffset at = row.file_offset + off_;
    while (!left.empty()) {
        auto r = job_.file->read_at(at, left);
        if (!r || r->empty())
            return cut_(h);
        at += proto::FileSize::of(*r);
        left = r->past_in(left);
    }
    key_.feed(piece);
    if (row.shaped)
        job_.loader->shape(row, piece, proto::kFileStart + off_);
    if (row.placed) {
        if (!job_.loader->place(row, piece, proto::kFileStart + off_, window_))
            return cut_(h);
    } else {
        const Ex<std::size_t> placed = row.stride == 1
            ? window_.write(off_.file_word<std::size_t>(), std::as_bytes(piece))
            : window_.write_lanes(
                  row.spread_bytes(off_).file_word<std::size_t>() + 2u * row.lane,
                  std::as_bytes(piece),
                  row.stride
              );
        if (!placed)
            return cut_(h);
        if (job_.mirror && !job_.mirror->write(off_.file_word<std::size_t>(), std::as_bytes(piece)))
            return cut_(h);
    }
    off_ += chunk;
    progress_(h, false);
    if (off_ == proto::FileSize{job_.total}) {
        if (row.placed) {
            pc_ = Pc::Conclude;
        } else {
            const bool more = proto::FileSize{extent_} > row.spread_bytes(off_);
            pc_ = more ? Pc::Fill : Pc::Finish;
        }
    }
    return Pass::Stepped;
}

LoadLadder::Pass LoadLadder::fill_(Host& h) {
    const proto::FileSize extent{extent_};
    fill_off_ = std::max(fill_off_, job_.row.spread_bytes(proto::FileSize{job_.total}));
    if (fill_off_ >= extent) {
        pc_ = Pc::Finish;
        return Pass::Stepped;
    }
    const proto::FileSize piece = (extent - fill_off_).min(proto::FileSize{buf_.size()});
    const std::span<std::uint8_t> tail = infra::affine::first(std::span<std::uint8_t>{buf_}, piece);
    std::ranges::fill(std::as_writable_bytes(tail), job_.row.fill);
    if (!window_.write(fill_off_.file_word<std::size_t>(), std::as_bytes(tail)))
        return cut_(h);
    if (job_.mirror
        && !job_.mirror->write(fill_off_.file_word<std::size_t>(), std::as_bytes(tail))) {
        return cut_(h);
    }
    fill_off_ += piece;
    return Pass::Stepped;
}

LoadLadder::Pass LoadLadder::conclude_(Host& h) {
    auto more = job_.loader->conclude(job_.row, window_);
    if (!more)
        return cut_(h);
    if (!*more)
        pc_ = Pc::Finish;
    return Pass::Stepped;
}

LoadLadder::Pass LoadLadder::finish_() {
    if (!job_.loader->finish(infra::OptRef<cores::ICoreWindow>{window_})) {
        pc_ = Pc::Cut;
        return Pass::Stepped;
    }

    window_.handoff();
    if (job_.mirror)
        job_.mirror->handoff();
    pc_ = Pc::Facts;
    return Pass::Stepped;
}

LoadLadder::Pass LoadLadder::facts_(Host& h) {
    const std::span<const std::uint8_t> facts = job_.loader->facts();
    if (!facts.empty()) {
        if (h.inbox.ring().size() >= proto::kLinkTxCapacity)
            return Pass::Waiting;

        const auto sent = h.inbox.push_load_facts(facts, path_.view());
        if (!sent)
            return sent.error().code == Errc::would_block ? Pass::Waiting : cut_(h);
        facts_pushed_ = true;
    }

    const bool mounts = !job_.save.empty() && job_.row.bracket != cores::RowBracket::None;
    pc_ = mounts ? Pc::Mount : Pc::Close;
    return Pass::Stepped;
}

LoadLadder::Pass LoadLadder::mount_(Host& h) {
    if (h.inbox.ring().size() >= proto::kLinkTxCapacity)
        return Pass::Waiting;
    auto id = h.inbox.intern_file_path(job_.save.view(), job_.save_bytes);
    if (!id) {
        if (id.error().code == Errc::would_block)
            return Pass::Waiting;
        pc_ = Pc::Close;
        return Pass::Stepped;
    }
    if (!h.inbox.push(
            proto::LinkOp::BindSlot{
                .slot = proto::SlotIndex{0},
                .bind = proto::LinkOp::SlotBind::Mount,
                .path = *id
            }
        )) {
        return Pass::Waiting;
    }
    pc_ = Pc::Close;
    return Pass::Stepped;
}

LoadLadder::Pass LoadLadder::close_(Host& h) {
    if (job_.row.bracket == cores::RowBracket::None)
        return notify_(h);
    auto id = h.inbox.intern_file_path(
        path_.view(),
        proto::FileSize{job_.total},
        key_.value(),
        0,
        job_.edge
    );
    if (!id)
        return id.error().code == Errc::would_block ? Pass::Waiting : cut_(h);

    if (facts_pushed_)
        (void)h.inbox.stamp_remount(*id);
    const proto::LinkOp::FileTx op{
        .wire_index = job_.wire_index.fio_byte(),
        .phase = proto::LinkOp::FileTxPhase::WindowClose,
        .file = *id,
        .total = bracket_length_().detail()
    };
    if (!h.inbox.push(op))
        return Pass::Waiting;
    pc_ = Pc::Over;
    progress_(h, true);
    return Pass::Done;
}

LoadLadder::Pass LoadLadder::notify_(Host& h) {
    const cores::PostNotify& n = job_.row.notify;
    if (n.armed) {
        if (h.inbox.ring().size() >= proto::kLinkTxCapacity)
            return Pass::Waiting;

        std::vector<std::uint8_t> words(n.words.size() * sizeof(std::uint16_t));
        infra::store_le_run(std::span<const std::uint16_t>(n.words), std::span(words));
        auto id = h.inbox.intern_file(FileIntern{.bytes = std::move(words), .path = path_.view()});
        if (!id)
            return id.error().code == Errc::would_block ? Pass::Waiting : cut_(h);
        const proto::LinkOp::StagePayload op{
            .payload = *id,
            .dest = proto::WideIoIndex{cores::PostNotify::kIndex},
            .copy_word = n.copy_word()
        };
        if (!h.inbox.push(op))
            return Pass::Waiting;
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
    const proto::LinkOp::FileTx op{
        .wire_index = job_.wire_index.fio_byte(),
        .phase = proto::LinkOp::FileTxPhase::WindowClose,
        .total = static_cast<std::uint32_t>(job_.total)
    };
    if (!h.inbox.push(op))
        return Pass::Waiting;
    pc_ = Pc::Over;
    progress_(h, true);
    return Pass::Failed;
}

proto::FileSize LoadLadder::bracket_length_() const noexcept {
    return job_.row.bracket_length(proto::FileSize{extent_});
}

void LoadLadder::progress_(Host& h, bool edge) noexcept {
    std::uint16_t cur = 0;
    std::uint16_t max = 0;
    if (!edge) {
        max = cores::kProgressSteps;
        cur = static_cast<std::uint16_t>(
            off_.file_word<std::uint64_t>() * cores::kProgressSteps / job_.total
        );
    }
    h.events.push(
        Event::ProgressUpdate{.progress = {.done = cur, .total = max}},
        Event::Head{ERR_SITE(), {}, kUncaused}
    );
}

Rung::Pass LoadLadder::step(const RungEdges& edges, cores::ILadderHost&) {
    TASTY_SEAT_BODY(LoadLadder);
    Host h{edges.inbox, edges.level, edges.events};
    switch (step(h)) {
        case Pass::Stepped:
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
