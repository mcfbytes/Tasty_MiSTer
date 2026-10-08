// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/session_params.h"
#include "proto/types.h"

namespace mister::proto {

class DownloadSession;

class FioBracket {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] static Ex<FioBracket> begin(hal::ISpiTransport& link, WideIoIndex index,
                                              hal::SpiWord mode, const SessionParams& params);
    [[nodiscard]] static Ex<FioBracket> window(hal::ISpiTransport& link, WideIoIndex index,
                                               hal::SpiWord mode);

    ~FioBracket();
    FioBracket(FioBracket&& o) noexcept
        : link_(std::exchange(o.link_, nullptr)), index_(o.index_) {}
    FioBracket& operator=(FioBracket&&) = delete;

    [[nodiscard]] Ex<void> end();
    [[nodiscard]] bool active() const noexcept { return link_ != nullptr; }
    [[nodiscard]] hal::ISpiTransport* link() const noexcept { return link_; }
    [[nodiscard]] std::uint16_t index() const noexcept { return index_; }

private:
    friend class DownloadSession;
    void detach() noexcept { link_ = nullptr; }
    FioBracket(hal::ISpiTransport& link, std::uint16_t index) : link_(&link), index_(index) {}
    hal::ISpiTransport* link_;
    std::uint16_t index_;
};

}  // namespace mister::proto
