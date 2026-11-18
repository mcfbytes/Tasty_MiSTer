// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/status_change.h"
#include "proto/status_source.h"
#include "proto/status_word.h"

namespace mister::proto {

class SpiStatusSource final : public IStatusSource {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit SpiStatusSource(hal::ISpiTransport& link) noexcept : link_(link) {}

    [[nodiscard]] std::optional<StatusChange> take(
        StatusChange held,
        StatusWord& words
    ) noexcept override;

private:
    hal::ISpiTransport& link_;
};

}
