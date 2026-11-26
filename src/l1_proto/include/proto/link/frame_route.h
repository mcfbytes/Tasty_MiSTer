// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/spi_transport.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/link/frame_lane.h"
#include "proto/link/spi_frame.h"
#include "proto/link_frame.h"

namespace mister::proto::link {

class FrameRoute {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit FrameRoute(hal::ISpiTransport& link) noexcept : spi_(link) {}

    FrameRoute(const FrameRoute&) = delete;
    FrameRoute& operator=(const FrameRoute&) = delete;

    void bind(IFrameLane& lane) noexcept;
    void unbind() noexcept;

    [[nodiscard]] ILinkFrame& frame() noexcept;

    [[nodiscard]] bool off_link() const noexcept { return lane_.has_value() && !suspended_; }

    void settle(bool park_asked) noexcept;

    [[nodiscard]] bool busy() const noexcept;

    void suspend() noexcept;
    void resume() noexcept;

    [[nodiscard]] bool suspended() const noexcept { return suspended_; }

private:
    SpiFrame spi_;
    infra::OptRef<IFrameLane> lane_{};
    bool suspended_ = false;
};

}
