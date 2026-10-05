// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/scanout_channel.h"
#include "app/uio_burst.h"
#include "hal/spi_transport.h"
#include "infra/seat.h"

namespace mister::app {

class ScanoutRelay {
    TASTY_SEAT_RESIDENT(RT);

public:
    ScanoutRelay(hal::ISpiTransport& link, ScanoutChannel& channel) noexcept
        : link_(link), channel_(channel) {}
    ScanoutRelay(const ScanoutRelay&) = delete;
    ScanoutRelay& operator=(const ScanoutRelay&) = delete;

    static constexpr std::uint32_t kMaxRoundWords = 1u + UioBurst::kMaxWords;

    enum class Wire : std::uint8_t { Ready, Held, CoreStarting };

    bool collect() noexcept;
    void step(Wire wire) noexcept;

    [[nodiscard]] std::uint32_t take_round_words() noexcept {
        const std::uint32_t w = round_words_;
        round_words_ = 0;
        return w;
    }
    [[nodiscard]] std::uint32_t bursts() const noexcept { return bursts_; }
    [[nodiscard]] std::uint32_t failures() const noexcept { return failures_; }
    [[nodiscard]] std::uint32_t refused() const noexcept { return refused_; }

private:
    [[nodiscard]] UioBurst emit_(const UioBurst& ask) noexcept;
    void refuse_(const UioBurst& ask) noexcept;

    hal::ISpiTransport& link_;
    ScanoutChannel& channel_;
    std::optional<UioBurst> ask_{};
    std::optional<UioBurst> unsent_{};
    std::uint32_t round_words_ = 0;
    std::uint32_t bursts_ = 0;
    std::uint32_t failures_ = 0;
    std::uint32_t refused_ = 0;
};

}  // namespace mister::app
