// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "app/rec_write_status.h"
#include "app/sidecar_msg.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"

namespace mister::app {

class SidecarWriter {
    TASTY_SEAT_RESIDENT(RecWrite);

public:
    struct Wiring {
        SidecarRing* in = nullptr;
        RecWriteStatusCell* status = nullptr;
    };

    static constexpr std::size_t kBufBytes = 64u * 1024u;

    static constexpr std::size_t kSpillBytes = 64u * 1024u * 1024u;
    static constexpr unsigned kMsgsPerPass = 256;

    static constexpr const char* kHeader = "core_frame\theader_ctr\tcapture_ns\tdup_reason\tmovie_"
                                           "frame\thash\twidth\theight\tsegment\t"
                                           "avi_frame\n";

    explicit SidecarWriter(const Wiring& w) noexcept : w_(w) {}
    ~SidecarWriter() { unmap_(); }
    SidecarWriter(const SidecarWriter&) = delete;
    SidecarWriter& operator=(const SidecarWriter&) = delete;

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] int park_ms() const noexcept;

    void release() noexcept;

    [[nodiscard]] const RecWriteStatus& status() const noexcept { return st_; }

    [[nodiscard]] std::size_t mapped_bytes() const noexcept { return cap_; }

private:
    void open_(const SidecarMsg& m) noexcept;
    void row_(const SidecarMsg& m) noexcept;
    void close_() noexcept;
    void fail_(int err) noexcept;
    void flush_() noexcept;
    void sync_() noexcept;
    [[nodiscard]] bool grow_(std::size_t need) noexcept;
    void unmap_() noexcept;

    Wiring w_;
    UniqueFd fd_{};
    char* buf_ = nullptr;
    std::size_t cap_ = 0;
    std::size_t used_ = 0;
    RecWriteStatus st_{};
    bool dirty_ = false;
};

}  // namespace mister::app
