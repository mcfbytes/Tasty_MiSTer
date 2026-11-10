// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/avi_encoder.h"
#include "app/encode_status.h"
#include "app/raw_frame_slot.h"
#include "app/sidecar_msg.h"
#include "app/types.h"
#include "infra/park_timeout.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "svc/types.h"

namespace mister::app {

class FrameHasher {
    TASTY_SEAT_RESIDENT(Encode);

public:
    struct Wiring {

        RawFrameChannel& channel;
        SidecarRing& out;
        EncodeStatusCell& status;
        const os::IClock& clock;
        AviEncoder& video;
    };

    static constexpr infra::ParkTimeout kHeldPollMs{5};
    static constexpr unsigned kJobsPerPass = 4;

    explicit FrameHasher(const Wiring& w) noexcept : w_(w) {}

    FrameHasher(const FrameHasher&) = delete;
    FrameHasher& operator=(const FrameHasher&) = delete;

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept;

    [[nodiscard]] bool drained() const noexcept;

    [[nodiscard]] bool holding() const noexcept { return static_cast<bool>(job_); }

    [[nodiscard]] infra::ParkTimeout park_ms() const noexcept;

    void release() noexcept;

    [[nodiscard]] const EncodeStatus& status() const noexcept { return st_; }

    using Result = bool;
    Result on(const RawFrameSlot::Open& s, ControlGeneration head) noexcept;
    Result on(const RawFrameSlot::Frame& s, ControlGeneration head) noexcept;
    Result on(const RawFrameSlot::Close& s, ControlGeneration head) noexcept;
    Result misrouted(const RawFrameSlot& m) noexcept;

private:
    [[nodiscard]] bool emit_(const RawFrameSlot& s) noexcept;

    template <class A>
    [[nodiscard]] bool emit_runs_(const A& s, ControlGeneration gen) noexcept;

    [[nodiscard]] bool push_(const SidecarMsg& m) noexcept;

    [[nodiscard]] bool push_row_(const SidecarMsg::Row& row, ControlGeneration gen) noexcept;

    template <class A>
    [[nodiscard]] bool chunk_(
        const A& s,
        ControlGeneration gen,
        bool real,
        SidecarMsg::Row& row
    ) noexcept;

    [[nodiscard]] svc::Crc32 hash_rows(const RawFrameSlot::Frame& s) noexcept;

    Wiring w_;
    RawFrameChannel::Job job_{};
    std::uint32_t next_ = 0;
    bool hashed_ = false;
    bool chunked_ = false;
    svc::Crc32 cur_hash_{};
    svc::Crc32 prev_hash_{};
    EncodeStatus st_{};
    bool dirty_ = false;
};

}
