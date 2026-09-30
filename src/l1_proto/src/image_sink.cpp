// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/image_sink.h"

#include "proto/image_bracket.h"
#include "proto/spi_image_sink.h"

#include <utility>

namespace mister::proto {

Ex<ImageBracket> ImageBracket::open(IImageSink& sink, WideIoIndex index,
                                    const SessionParams& params) {
    ImageBracket b(sink);
    if (auto r = sink.begin(index, params); !r) {
        return std::unexpected(r.error());
    }
    b.open_ = true;
    return b;
}

Ex<void> ImageBracket::end() {
    if (!open_) return {};
    open_ = false;
    return sink_->end();
}

Ex<void> SpiImageSink::begin(WideIoIndex index, const SessionParams& params) {
    TASTY_SEAT_BODY(SpiImageSink);
    if (active()) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), index.v});
    }

    if (queue_ != nullptr && queue_->held()) {
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), index.v});
    }
    ds_.reset();
    if (queue_ != nullptr) (void)queue_->flush();
    auto s = DownloadSession::begin(*link_, index, params);
    if (!s) return std::unexpected(s.error());
    ds_.emplace(std::move(*s));

    if (queue_ != nullptr) queue_->hold();
    return {};
}

Ex<void> SpiImageSink::write(std::span<const std::uint8_t> data) {
    TASTY_SEAT_BODY(SpiImageSink);
    if (!active()) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    return ds_->write(data);
}

Ex<void> SpiImageSink::end() {
    TASTY_SEAT_BODY(SpiImageSink);
    if (!ds_) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    auto r = ds_->end();
    ds_.reset();
    if (queue_ != nullptr) queue_->release();
    return r;
}

}  // namespace mister::proto
