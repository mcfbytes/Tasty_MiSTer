// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/frame_source.h"
#include "app/video_pump.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "hal/fpga_memory.h"
#include "hal/boards_table.h"

namespace mister::app {

class ScalerFrameSource final : public IFrameSource {
    TASTY_SEAT_RESIDENT(Diag);

public:
    static constexpr SeatTag kSeat = SeatTag::Diag;

    static constexpr std::size_t kStockCaptureBytes = 2048u * 3u * 1024u;

    static constexpr std::uint32_t kMaxScaledWidth = 2304u;

    static constexpr std::uint32_t kMaxScaledHeight = 4095u;

    enum class Refusal : std::uint8_t {
        Unsupported,
        Corrupt,
        Oversize,
        Aperture,
        Count,
    };

    ScalerFrameSource(hal::FpgaMemory window, const VideoGeometryCell& geometry) noexcept;

    ScalerFrameSource(const ScalerFrameSource&) = delete;
    ScalerFrameSource& operator=(const ScalerFrameSource&) = delete;

    [[nodiscard]] Ex<Frame> capture(bool scaled) override;
    [[nodiscard]] Counts counts() const noexcept override {
        TASTY_SEAT_BODY(ScalerFrameSource);
        return Counts{captures_, refusals(), torn_};
    }

    [[nodiscard]] std::uint32_t captures() const noexcept { return captures_; }
    [[nodiscard]] std::uint32_t torn() const noexcept { return torn_; }
    [[nodiscard]] std::uint32_t refusals() const noexcept;
    [[nodiscard]] std::uint32_t refusals(Refusal why) const noexcept {
        return refused_[static_cast<std::size_t>(why)];
    }

private:
    static constexpr std::size_t kRefusalKinds = static_cast<std::size_t>(Refusal::Count);

    [[nodiscard]] std::unexpected<Error> refuse(Refusal why, std::uint16_t site) noexcept;

    hal::FpgaMemory window_;
    const VideoGeometryCell& geometry_;
    std::uint32_t captures_ = 0;
    std::uint32_t torn_ = 0;
    std::uint32_t refused_[kRefusalKinds] = {};
};

static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, ScalerFrameSource::kSeat).policy ==
                         hal::SchedPolicy::Other;
              }),
              "the capture blocks; it cannot run under SCHED_FIFO");
static_assert(ScalerFrameSource::kSeat == SeatTag::Diag,
              "the drain that calls capture() is T-DIAG's; two seats would be two readers");

}  // namespace mister::app
