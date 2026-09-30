// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/download_session.h"
#include "proto/image_sink.h"
#include "proto/spi_fio_queue.h"

namespace mister::proto {

class SpiImageSink final : public IImageSink {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit SpiImageSink(hal::ISpiTransport& link, SpiFioQueue* queue = nullptr) noexcept
        : link_(&link), queue_(queue) {}

    [[nodiscard]] Ex<void> begin(WideIoIndex index, const SessionParams& params) override;
    [[nodiscard]] Ex<void> write(std::span<const std::uint8_t> data) override;
    [[nodiscard]] Ex<void> end() override;

    [[nodiscard]] bool active() const noexcept { return ds_.has_value() && ds_->active(); }

private:
    hal::ISpiTransport* link_;
    SpiFioQueue* queue_;
    std::optional<DownloadSession> ds_;
};

}  // namespace mister::proto
