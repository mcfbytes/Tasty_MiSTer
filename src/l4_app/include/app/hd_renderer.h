// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "app/backdrop_sampler.h"
#include "app/backdrop_scaler.h"
#include "app/hd_dirty_ledger.h"
#include "app/hd_layout.h"
#include "app/hd_flip_slot.h"
#include "app/hd_osd_ask.h"
#include "app/hd_osd_status.h"
#include "app/hd_raster.h"
#include "app/hd_tick_pacer.h"
#include "app/motion_badge.h"
#include "hal/fpga_memory.h"
#include "infra/seat.h"
#include "os/clock.h"

namespace mister::app {

class HdRenderer {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    static constexpr std::uint64_t kFlipLatchNs = 50'000'000;
    static constexpr std::uint64_t kDeadlineNs = 1'000'000'000;
    static constexpr std::size_t kMaxFrameBytes = 4u * 1024u * 1024u;

    struct Wiring {
        const os::IClock* clock = nullptr;
        const HdOsdAskCell* ask = nullptr;
        HdOsdStatusCell* status = nullptr;
    };

    explicit HdRenderer(const Wiring& w, std::uint8_t fps = 10) noexcept : w_(w), pacer_(fps) {}
    HdRenderer(const HdRenderer&) = delete;
    HdRenderer& operator=(const HdRenderer&) = delete;
    HdRenderer(HdRenderer&&) = delete;
    HdRenderer& operator=(HdRenderer&&) = delete;

    void bind_frame(HdFlipChannel* flips, hal::FpgaMemory mem,
                    std::unique_ptr<HdRaster> raster) noexcept;

    void bind_scaler(hal::ScalerBuffers window) noexcept;

    void set_fps(std::uint8_t fps) noexcept;

    void begin() noexcept;
    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] int park_ms() const noexcept;
    void release() noexcept;

    [[nodiscard]] std::uint64_t ticks() const noexcept { return ticks_; }
    [[nodiscard]] const HdOsdStatus& status() const noexcept { return st_; }

private:
    void publish_() noexcept;
    [[nodiscard]] std::uint64_t now_ns_() const noexcept;
    void drop_held_() noexcept;
    void reap_(bool hold, std::uint64_t now) noexcept;
    [[nodiscard]] HdFlipChannel::Loan writable_(std::uint64_t now) noexcept;
    [[nodiscard]] bool ensure_frame_() noexcept;
    void paint_(std::uint64_t now) noexcept;
    [[nodiscard]] bool backdrop_(const HdLayout& layout, bool reset_frame) noexcept;
    void copy_rect_(std::uint8_t slot, HdRect rect) noexcept;

    Wiring w_{};
    HdTickPacer pacer_;
    HdOsdAskCell::Reader reader_{};
    HdOsdAsk ask_{};
    bool up_ = false;
    bool raster_open_ = false;
    std::uint64_t ticks_ = 0;
    std::uint32_t epoch_seen_ = 0;
    std::uint32_t seq_ = 0;
    HdOsdStatus st_{};

    HdFlipChannel* flips_ = nullptr;
    std::optional<hal::FpgaMemory> slots_{};
    std::unique_ptr<HdRaster> raster_{};
    std::optional<BackdropSampler> sampler_{};
    BackdropScaler scaler_{};
    MotionBadge badge_{};
    HdDirtyLedger ledger_{};
    std::vector<std::uint16_t> frame_{};
    std::array<HdFlipChannel::Loan, 2> held_{};
    std::array<std::uint64_t, 2> held_at_{};
};

}  // namespace mister::app
