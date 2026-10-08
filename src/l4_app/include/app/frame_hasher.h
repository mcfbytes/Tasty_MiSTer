// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/avi_encoder.h"
#include "app/encode_status.h"
#include "app/raw_frame_slot.h"
#include "app/sidecar_msg.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "os/clock.h"

namespace mister::app {

class FrameHasher {
    TASTY_SEAT_RESIDENT(Encode);

public:
    struct Wiring {
        RawFrameChannel* channel = nullptr;
        SidecarRing* out = nullptr;
        xthread::WakeFlag* writer_wake = nullptr;
        EncodeStatusCell* status = nullptr;
        const os::IClock* clock = nullptr;
        AviEncoder* video = nullptr;
    };

    static constexpr int kHeldPollMs = 5;
    static constexpr unsigned kJobsPerPass = 4;

    explicit FrameHasher(const Wiring& w) noexcept : w_(w) {}
    FrameHasher(const FrameHasher&) = delete;
    FrameHasher& operator=(const FrameHasher&) = delete;

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept;

    [[nodiscard]] bool drained() const noexcept;
    [[nodiscard]] bool holding() const noexcept { return static_cast<bool>(job_); }
    [[nodiscard]] int park_ms() const noexcept;

    void release() noexcept;

    [[nodiscard]] const EncodeStatus& status() const noexcept { return st_; }

    [[nodiscard]] static std::uint32_t hash_rows(const RawFrameSlot& s) noexcept;

private:
    [[nodiscard]] bool emit_(const RawFrameSlot& s) noexcept;
    [[nodiscard]] bool emit_runs_(const RawFrameSlot& s, SidecarMsg& m) noexcept;
    [[nodiscard]] bool push_(const SidecarMsg& m) noexcept;

    [[nodiscard]] bool chunk_(const RawFrameSlot& s, bool real, SidecarMsg& m) noexcept;

    Wiring w_;
    RawFrameChannel::Job job_{};
    std::uint32_t next_ = 0;
    bool hashed_ = false;
    bool chunked_ = false;
    std::uint32_t cur_hash_ = 0;
    std::uint32_t prev_hash_ = 0;
    EncodeStatus st_{};
    bool dirty_ = false;
};

}  // namespace mister::app
