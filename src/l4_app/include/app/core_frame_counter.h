// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/core_frame_record.h"
#include "app/frame_demand.h"
#include "hal/spi_transport.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "proto/frame_counter_read.h"

namespace mister::app {

class CoreFrameCounter {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Wiring {
        hal::ISpiTransport* link = nullptr;
        const os::IClock* clock = nullptr;
        const FrameDemandCell* demand = nullptr;
        CoreFrameCell* out = nullptr;
    };

    static constexpr std::int64_t kRebaseNs = 1'000'000'000;

    static constexpr std::uint32_t kMaxRoundWords = proto::FrameCounterRead::kReadWords;

    explicit CoreFrameCounter(const Wiring& w) noexcept : w_(w) {}
    CoreFrameCounter(const CoreFrameCounter&) = delete;
    CoreFrameCounter& operator=(const CoreFrameCounter&) = delete;

    [[nodiscard]] Ex<proto::FrameCount> read() noexcept;

    void settle(bool ready, std::uint32_t core_seq, std::int32_t movie_frame) noexcept;

    [[nodiscard]] std::uint32_t take_round_words() noexcept {
        const std::uint32_t w = round_words_;
        round_words_ = 0;
        return w;
    }
    [[nodiscard]] bool demanded() const noexcept { return demand_.on != 0; }
    [[nodiscard]] const CoreFrameRecord& record() const noexcept { return rec_; }
    [[nodiscard]] std::uint32_t reads() const noexcept { return counter_.reads(); }

private:
    void sample_demand_() noexcept;

    Wiring w_;
    proto::FrameCounterRead counter_{};
    FrameDemandCell::Reader demand_seen_{};
    FrameDemand demand_{};
    CoreFrameRecord rec_{};
    std::uint8_t last_raw_ = 0;
    bool based_ = false;
    bool read_this_round_ = false;
    bool dirty_ = false;
    std::int64_t last_read_ns_ = 0;
    std::uint32_t round_words_ = 0;
};

}  // namespace mister::app
