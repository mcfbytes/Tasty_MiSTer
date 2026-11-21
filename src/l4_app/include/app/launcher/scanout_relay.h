// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "app/round_words.h"
#include "app/launcher/scanout_channel.h"
#include "app/launcher/uio_burst.h"
#include "hal/spi_transport.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::app::launcher {

class ScanoutRelay {
    TASTY_SEAT_RESIDENT(RT);

public:
    ScanoutRelay(hal::ISpiTransport& link, ScanoutChannel::Worker channel) noexcept
        : link_(link),
          channel_(std::move(channel)) {}

    ScanoutRelay(const ScanoutRelay&) = delete;
    ScanoutRelay& operator=(const ScanoutRelay&) = delete;

    static constexpr proto::FioWordCount kMaxRoundWords{1u + UioBurst::kMaxWords};

    enum class Wire : std::uint8_t { Ready, Held, CoreStarting };

    bool collect() noexcept;
    void step(Wire wire) noexcept;

    [[nodiscard]] proto::FioWordCount take_round_words() noexcept { return round_words_.take(); }

    [[nodiscard]] std::uint32_t bursts() const noexcept { return bursts_; }

    [[nodiscard]] std::uint32_t failures() const noexcept { return failures_; }

    [[nodiscard]] std::uint32_t refused() const noexcept { return refused_; }

private:
    void emit_(UioBurst& burst) noexcept;
    void refuse_(UioBurst& burst) noexcept;

    hal::ISpiTransport& link_;
    ScanoutChannel::Worker channel_;
    ScanoutChannel::Job ask_{};
    RoundWords round_words_{};
    std::uint32_t bursts_ = 0;
    std::uint32_t failures_ = 0;
    std::uint32_t refused_ = 0;
};

}
