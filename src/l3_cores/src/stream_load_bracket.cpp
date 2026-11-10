// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/stream_load_bracket.h"

#include <algorithm>
#include <utility>

#include "cores/file_tx.h"

namespace mister::cores {

Ex<StreamLoadBracket> StreamLoadBracket::open(
    IStreamLoad& core,
    proto::IImageSink& sink,
    IoIndex index,
    StreamLoadKind kind,
    std::uint32_t length
) {
    auto params = core.stream_opening(index);
    if (!params)
        return std::unexpected(params.error());
    if (length != 0)
        params->aux = proto::AuxValue{length};
    auto b = proto::ImageBracket::open(sink, proto::WideIoIndex{index.v}, *params);
    if (!b)
        return std::unexpected(b.error());
    return StreamLoadBracket(index, kind, std::move(*b));
}

StreamLoadBracket::StreamLoadBracket(StreamLoadBracket&& o) noexcept
    : index_(o.index_),
      kind_(o.kind_),
      bracket_(std::move(o.bracket_)) {
    o.bracket_.reset();
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"

StreamLoadBracket::~StreamLoadBracket() {
    cut_();
}

#pragma GCC diagnostic pop

void StreamLoadBracket::cut_() noexcept {
    if (!bracket_)
        return;
    (void)bracket_->end();
    bracket_.reset();
}

Ex<void> StreamLoadBracket::write(std::span<const std::uint8_t> piece) {
    TASTY_SEAT_BODY(StreamLoadBracket);
    if (!bracket_)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), index_.v});
    while (!piece.empty()) {
        const std::size_t n = std::min(piece.size(), kFileTxChunkBytes);
        if (auto r = bracket_->write(piece.first(n)); !r)
            return r;
        piece = piece.subspan(n);
    }
    return {};
}

Ex<std::span<const std::uint8_t>> StreamLoadBracket::write_taken(
    std::span<const std::uint8_t> piece
) {
    TASTY_SEAT_BODY(StreamLoadBracket);
    if (!bracket_)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), index_.v});
    std::size_t taken = 0;
    while (taken < piece.size()) {
        const std::size_t n = std::min(piece.size() - taken, kFileTxChunkBytes);
        auto r = bracket_->write_taken(piece.subspan(taken, n));
        if (!r)
            return std::unexpected(r.error());
        taken += *r;
        if (*r < n)
            break;
    }
    return piece.first(taken);
}

Ex<void> StreamLoadBracket::close(
    infra::OptRef<IStreamLoad> live,
    proto::FileSize bytes,
    svc::Crc32 crc
) {
    TASTY_SEAT_BODY(StreamLoadBracket);
    if (!bracket_)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), index_.v});
    auto r = bracket_->end();
    bracket_.reset();
    if (live) {
        live->stream_closed(
            r ? StreamLoadEnd{.index = index_, .bytes = bytes, .crc = crc, .ok = true}
              : StreamLoadEnd{.index = index_}
        );
    }
    return r;
}

void StreamLoadBracket::cut(infra::OptRef<IStreamLoad> live) noexcept {
    TASTY_SEAT_BODY(StreamLoadBracket);
    if (!bracket_)
        return;
    (void)bracket_->end();
    bracket_.reset();
    if (live)
        live->stream_closed(StreamLoadEnd{.index = index_});
}

}
