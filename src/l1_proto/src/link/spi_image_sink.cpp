// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/spi_image_sink.h"

#include <cstddef>
#include <utility>

#include "infra/prelude.h"
#include "proto/link/spi_fio_queue.h"

namespace mister::proto::link {

Ex<void> SpiImageSink::begin(WideIoIndex index, const SessionParams& params) {
    TASTY_SEAT_BODY(SpiImageSink);
    if (active()) {
        return std::unexpected(index.refusal(Errc::negotiation, ERR_SITE()));
    }

    if (queue_ && queue_->held()) {
        return std::unexpected(index.refusal(Errc::would_block, ERR_SITE()));
    }

    if (queue_ && queue_->tenant_holds_fio()) {
        return std::unexpected(index.refusal(Errc::would_block, ERR_SITE()));
    }
    ds_.reset();
    if (queue_)
        (void)queue_->flush();
    auto s = DownloadSession::begin(link_, index, params);
    if (!s)
        return std::unexpected(s.error());
    ds_.emplace(std::move(*s));

    if (queue_)
        queue_->hold();
    return {};
}

Ex<void> SpiImageSink::write(std::span<const std::uint8_t> data) {
    TASTY_SEAT_BODY(SpiImageSink);
    if (!active()) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    return ds_->write(data);
}

Ex<std::size_t> SpiImageSink::write_taken(std::span<const std::uint8_t> data) {
    TASTY_SEAT_BODY(SpiImageSink);
    if (!active()) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    return ds_->write_taken(data);
}

Ex<void> SpiImageSink::end() {
    TASTY_SEAT_BODY(SpiImageSink);
    if (!ds_) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    auto r = ds_->end();
    ds_.reset();
    if (queue_)
        queue_->release();
    return r;
}

}
