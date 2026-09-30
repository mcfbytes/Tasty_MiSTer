// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app/rec_write_status.h"
#include "app/sidecar_msg.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"
#include "os/clock.h"

namespace mister::app {

class SidecarWriter {
    TASTY_SEAT_RESIDENT(RecWrite);

public:
    struct Wiring {
        SidecarRing* in = nullptr;
        RecWriteStatusCell* status = nullptr;
        const os::IClock* clock = nullptr;
    };

    static constexpr std::size_t kBufBytes = 64u * 1024u;

    static constexpr std::size_t kFlushBytes = 48u * 1024u;
    static constexpr std::int64_t kFlushNs = 250'000'000;
    static constexpr std::int64_t kSyncNs = 5'000'000'000;
    static constexpr unsigned kMsgsPerPass = 256;

    static constexpr const char* kHeader = "core_frame\theader_ctr\tcapture_ns\tdup_reason\tmovie_"
                                           "frame\thash\twidth\theight\tsegment\t"
                                           "avi_frame\n";

    explicit SidecarWriter(const Wiring& w) noexcept : w_(w) {}
    SidecarWriter(const SidecarWriter&) = delete;
    SidecarWriter& operator=(const SidecarWriter&) = delete;

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] int park_ms() const noexcept;

    void release() noexcept;

    [[nodiscard]] const RecWriteStatus& status() const noexcept { return st_; }

private:
    void open_(const SidecarMsg& m) noexcept;
    void row_(const SidecarMsg& m) noexcept;
    void close_() noexcept;
    void fail_(int err) noexcept;
    void flush_() noexcept;
    void sync_(std::int64_t now) noexcept;
    [[nodiscard]] std::int64_t now_() const noexcept;

    Wiring w_;
    UniqueFd fd_{};
    std::array<char, kBufBytes> buf_{};
    std::size_t used_ = 0;
    std::int64_t oldest_ns_ = 0;
    std::int64_t synced_ns_ = 0;
    RecWriteStatus st_{};
    bool dirty_ = false;
};

}  // namespace mister::app
