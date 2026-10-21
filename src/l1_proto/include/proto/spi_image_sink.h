// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/download_session.h"
#include "proto/image_sink.h"
#include "proto/spi_fio_queue.h"

namespace mister::proto {

class SpiImageSink final : public IImageSink {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit SpiImageSink(hal::ISpiTransport& link, infra::OptRef<SpiFioQueue> queue = {}) noexcept
        : link_(&link),
          queue_(queue) {}

    [[nodiscard]] Ex<void> begin(WideIoIndex index, const SessionParams& params) override;
    [[nodiscard]] Ex<void> write(std::span<const std::uint8_t> data) override;
    [[nodiscard]] Ex<void> end() override;
    [[nodiscard]] Ex<std::size_t> write_taken(std::span<const std::uint8_t> data) override;

    [[nodiscard]] bool active() const noexcept { return ds_.has_value() && ds_->active(); }

private:
    hal::ISpiTransport* link_;
    infra::OptRef<SpiFioQueue> queue_;
    std::optional<DownloadSession> ds_;
};

}
