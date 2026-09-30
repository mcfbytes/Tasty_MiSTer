// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/stream_load_bracket.h"

#include <algorithm>
#include <utility>

#include "cores/file_tx.h"

namespace mister::cores {

Ex<StreamLoadBracket> StreamLoadBracket::open(IStreamLoad& core, proto::IImageSink& sink,
                                              IoIndex index, std::uint32_t length) {
    auto params = core.stream_opening(index);
    if (!params) return std::unexpected(params.error());
    if (length != 0) params->aux = proto::AuxValue{length};
    auto b = proto::ImageBracket::open(sink, proto::WideIoIndex{index.v}, *params);
    if (!b) return std::unexpected(b.error());
    return StreamLoadBracket(core, index, std::move(*b));
}

StreamLoadBracket::StreamLoadBracket(StreamLoadBracket&& o) noexcept
    : core_(o.core_), index_(o.index_), bracket_(std::move(o.bracket_)) {
    o.bracket_.reset();
}

StreamLoadBracket::~StreamLoadBracket() { cut_(); }

void StreamLoadBracket::cut_() noexcept {
    if (!bracket_) return;
    (void)bracket_->end();
    bracket_.reset();
    core_->stream_closed(StreamLoadEnd{.index = index_});
}

Ex<void> StreamLoadBracket::write(std::span<const std::uint8_t> piece) {
    TASTY_SEAT_BODY(StreamLoadBracket);
    if (!bracket_) return std::unexpected(Error{Errc::negotiation, ERR_SITE(), index_.v});
    while (!piece.empty()) {
        const std::size_t n = std::min(piece.size(), kFileTxChunkBytes);
        if (auto r = bracket_->write(piece.first(n)); !r) return r;
        piece = piece.subspan(n);
    }
    return {};
}

Ex<void> StreamLoadBracket::close(std::uint64_t bytes, std::uint32_t crc) {
    TASTY_SEAT_BODY(StreamLoadBracket);
    if (!bracket_) return std::unexpected(Error{Errc::negotiation, ERR_SITE(), index_.v});
    auto r = bracket_->end();
    bracket_.reset();
    core_->stream_closed(r ? StreamLoadEnd{.index = index_, .bytes = bytes, .crc = crc, .ok = true}
                           : StreamLoadEnd{.index = index_});
    return r;
}

}  // namespace mister::cores
