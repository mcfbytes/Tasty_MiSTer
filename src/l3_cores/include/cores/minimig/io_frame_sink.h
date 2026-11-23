// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "hal/spi_transport.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/image_sink.h"
#include "proto/session_params.h"
#include "proto/types.h"

namespace mister::cores::minimig {

class IoFrameSink final : public proto::IImageSink {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Tally {
        infra::Tally frames{};
        infra::Tally refused{};
        infra::Tally faults{};
    };

    explicit IoFrameSink(hal::ISpiTransport& link) noexcept : link_(link) {}

    [[nodiscard]] Ex<void> begin(
        proto::WideIoIndex index,
        const proto::SessionParams& params
    ) override;

    [[nodiscard]] Ex<void> write(std::span<const std::uint8_t> data) override;
    [[nodiscard]] Ex<void> end() override;

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    hal::ISpiTransport& link_;
    Tally tally_{};
};

}
