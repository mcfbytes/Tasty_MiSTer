// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/core_frame_record.h"
#include "app/frame_demand.h"
#include "app/round_words.h"
#include "hal/spi_transport.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "proto/link/frame_counter_read.h"
#include "proto/types.h"

namespace mister::app {

class CoreFrameCounter {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Wiring {
        hal::ISpiTransport& link;
        const os::IClock& clock;
        infra::OptRef<const FrameDemandCell> demand{};
        infra::OptRef<CoreFrameCell> out{};
    };

    static constexpr std::int64_t kRebaseNs = 1'000'000'000;

    static constexpr proto::FioWordCount kMaxRoundWords = proto::link::FrameCounterRead::kReadWords;

    explicit CoreFrameCounter(const Wiring& w) noexcept : w_(w) {}

    CoreFrameCounter(const CoreFrameCounter&) = delete;
    CoreFrameCounter& operator=(const CoreFrameCounter&) = delete;

    [[nodiscard]] Ex<proto::FrameCount> read() noexcept;

    void settle(bool ready, CoreEdgeSeq core_seq, MoviePosition movie_frame) noexcept;

    [[nodiscard]] proto::FioWordCount take_round_words() noexcept { return round_words_.take(); }

    [[nodiscard]] bool demanded() const noexcept { return demand_.on; }

    [[nodiscard]] const CoreFrameRecord& record() const noexcept { return rec_; }

    [[nodiscard]] std::uint32_t reads() const noexcept { return counter_.reads(); }

private:
    void sample_demand_() noexcept;

    Wiring w_;
    proto::link::FrameCounterRead counter_{};
    FrameDemandCell::Reader demand_seen_{};
    FrameDemand demand_{};
    CoreFrameRecord rec_{};
    std::uint8_t last_raw_ = 0;
    bool based_ = false;
    bool read_this_round_ = false;
    bool dirty_ = false;
    os::RawMonotonicClock::time_point last_read_at_{};
    RoundWords round_words_{};
};

}
