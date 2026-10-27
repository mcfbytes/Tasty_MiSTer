// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "app/backdrop_job.h"
#include "app/backdrop_sampler.h"
#include "app/backdrop_scaler.h"
#include "app/hd_dirty_ledger.h"
#include "app/hd_layout.h"
#include "app/hd_flip_slot.h"
#include "app/hd_glass.h"
#include "app/hd_osd_ask.h"
#include "app/hd_osd_status.h"
#include "app/hd_raster.h"
#include "app/hd_theme.h"
#include "app/hd_themes.h"
#include "app/hd_tick_pacer.h"
#include "app/hd_visible_stats.h"
#include "hal/fpga_memory.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"

namespace mister::app {

class HdRenderer {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    static constexpr std::uint64_t kFlipLatchNs = 50'000'000;
    static constexpr std::uint64_t kDeadlineNs = 1'000'000'000;
    static constexpr std::size_t kMaxFrameBytes = 4u * 1024u * 1024u;

    static constexpr std::uint64_t kSliceNs = 2'000'000;

    struct Wiring {
        const os::IClock& clock;
        const HdOsdAskCell& ask;
        HdOsdStatusCell& status;
    };

    struct Frame {
        HdFlipChannel& flips;
        hal::FpgaMemory mem;
        HdRasters rasters;
    };

    explicit HdRenderer(const Wiring& w, std::uint8_t fps = 10) noexcept
        : HdRenderer(w, std::nullopt, fps) {}

    HdRenderer(const Wiring& w, std::optional<Frame> frame, std::uint8_t fps = 10) noexcept;
    HdRenderer(const HdRenderer&) = delete;
    HdRenderer& operator=(const HdRenderer&) = delete;
    HdRenderer(HdRenderer&&) = delete;
    HdRenderer& operator=(HdRenderer&&) = delete;

    void bind_scaler(hal::ScalerBuffers window) noexcept;

    void set_fps(std::uint8_t fps) noexcept;

    void set_look(HdLook look) noexcept;

    void begin() noexcept;
    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] int park_ms() const noexcept;
    void release() noexcept;

    [[nodiscard]] std::uint64_t ticks() const noexcept { return ticks_; }

    [[nodiscard]] const HdOsdStatus& status() const noexcept { return st_; }

    [[nodiscard]] BackdropJob::Step job_step() const noexcept { return job_.step(); }

    [[nodiscard]] HdLook look() const noexcept { return look_; }

private:
    static constexpr std::uint8_t kNoSlot = 0xFF;

    static constexpr std::array<HdDirtyLedger::Region, 2> kCards{
        HdDirtyLedger::Region::Panel,
        HdDirtyLedger::Region::Band
    };

    void publish_() noexcept;
    [[nodiscard]] std::uint64_t now_ns_() const noexcept;
    [[nodiscard]] bool ask_moved_() const noexcept;
    void reap_(bool hold, std::uint64_t now) noexcept;
    [[nodiscard]] bool owned_(std::uint8_t slot, std::uint64_t now) const noexcept;
    [[nodiscard]] bool stale_(std::uint8_t slot) const noexcept;
    [[nodiscard]] bool cards_stale_(std::uint8_t slot) const noexcept;
    [[nodiscard]] bool spills_(const HdDirty& dirty, const HdLayout& layout) const noexcept;
    [[nodiscard]] std::uint8_t flip_target_(std::uint64_t now) const noexcept;
    [[nodiscard]] bool flip_wanted_() const noexcept;
    [[nodiscard]] bool can_progress_(std::uint64_t now) const noexcept;
    [[nodiscard]] std::uint64_t wait_ns_(std::uint64_t now) const noexcept;
    [[nodiscard]] bool ensure_frame_() noexcept;
    void close_job_() noexcept;
    [[nodiscard]] std::unique_ptr<HdRaster>& raster_() noexcept;
    [[nodiscard]] bool retheme_(HdLook look) noexcept;
    void draw_(std::uint64_t now) noexcept;
    void start_epoch_(const HdLayout& layout) noexcept;
    void panel_step_(const HdLayout& layout, bool asked) noexcept;
    void place_(std::uint64_t now, bool wrote = false) noexcept;
    [[nodiscard]] std::int64_t in_place_px_() const noexcept;
    [[nodiscard]] bool copy_stale_(std::uint64_t now) noexcept;
    bool flip_(std::uint64_t now) noexcept;
    [[nodiscard]] bool slice_(std::uint64_t now) noexcept;
    [[nodiscard]] bool read_(std::uint64_t now) noexcept;
    void visible_(std::uint64_t now, bool by_flip) noexcept;
    [[nodiscard]] BackdropJob::Canvas canvas_() noexcept;
    void copy_rect_(std::uint8_t slot, HdRect rect) noexcept;

    Wiring w_;
    HdTickPacer pacer_;
    HdOsdAskCell::Reader reader_{};
    HdOsdAsk ask_{};
    bool up_ = false;
    bool raster_open_ = false;
    bool scaler_uio_ = false;
    bool epoch_valid_ = false;
    bool fresh_ask_ = false;
    bool panel_owed_ = false;
    bool bd_reset_ = false;
    bool pending_ = false;
    bool deadline_ = false;
    bool canvas_fresh_ = false;
    std::uint64_t ticks_ = 0;
    std::uint32_t epoch_seen_ = 0;
    std::uint32_t seq_ = 0;
    HdOsdStatus st_{};
    HdLook look_{};
    HdTheme theme_ = hd_theme_tokens(HdLook{});

    infra::OptRef<HdFlipChannel> flips_{};
    std::optional<hal::FpgaMemory> slots_{};
    HdRasters rasters_{};
    std::optional<BackdropSampler> sampler_{};
    BackdropScaler scaler_{};
    BackdropJob job_{scaler_};
    HdGlass glass_{};
    HdDirtyLedger ledger_{};
    HdVisibleStats vis_{};
    std::vector<std::uint16_t> frame_{};
    HdLayout layout_{};
    std::array<HdFlipChannel::Loan, 2> own_{};
    std::array<std::uint64_t, 2> free_at_{};
    std::array<std::uint32_t, 2> slot_gen_{};
    std::uint8_t last_sent_ = kNoSlot;
    std::uint32_t canvas_gen_ = 0;
    std::uint32_t fill_gen_ = 0;
    std::uint32_t shown_gen_ = 0;
    std::uint64_t pending_stamp_ = 0;
    std::uint64_t job_started_ = 0;
    std::uint64_t gen_started_ = 0;
    std::uint64_t scale_ns_ = 0;
    std::uint64_t copy_ns_ = 0;
};

}
