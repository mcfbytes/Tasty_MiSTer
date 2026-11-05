// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "cores/pcm_feed_state.h"
#include "infra/error.h"
#include "svc/types.h"

namespace mister::cores {

class IPcmFeed {
public:
    virtual ~IPcmFeed() = default;

    [[nodiscard]] virtual Ex<void> play(svc::TrackNumber track, bool loop) = 0;
    [[nodiscard]] virtual Ex<void> stop() = 0;
    [[nodiscard]] virtual Ex<void> resume() = 0;

    virtual void observe_read_point(std::uint32_t off) noexcept = 0;

    [[nodiscard]] virtual PcmFeedState state() const noexcept = 0;

    [[nodiscard]] virtual bool can_serve() const noexcept = 0;

    [[nodiscard]] virtual bool ready_for_source() noexcept = 0;

    [[nodiscard]] virtual Ex<void> set_sheet(std::string_view sheet) = 0;

protected:
    IPcmFeed() = default;
    IPcmFeed(const IPcmFeed&) = default;
    IPcmFeed& operator=(const IPcmFeed&) = default;
};

}
