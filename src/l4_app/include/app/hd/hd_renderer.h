// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "app/hd/backdrop_job.h"
#include "app/hd/backdrop_sampler.h"
#include "app/hd/backdrop_scaler.h"
#include "app/hd/band_fade_pattern.h"
#include "app/hd/band_gradient_pattern.h"
#include "app/hd/black_pattern.h"
#include "app/hd/canvas_gen.h"
#include "app/hd/front_end_pattern.h"
#include "app/hd/hd_badge_view.h"
#include "app/hd/hd_dirty_ledger.h"
#include "app/hd/flip_buffer.h"
#include "app/hd/front_end_logo.h"
#include "app/hd/hd_flip_holds.h"
#include "app/hd/hd_flip_slot.h"
#include "app/hd/hd_layout.h"
#include "app/hd/hd_glass.h"
#include "app/hd/hd_osd_ask.h"
#include "app/hd/hd_osd_status.h"
#include "app/hd/hd_raster.h"
#include "app/hd/hd_theme.h"
#include "app/hd/hd_themes.h"
#include "app/hd/hd_tick_pacer.h"
#include "app/hd/hd_visible_stats.h"
#include "app/hd/spectrum_pattern.h"
#include "app/hd/wallpaper_pattern.h"
#include "hal/fpga_memory.h"
#include "infra/cell_gen.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/park_timeout.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "os/clock.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app::hd {

class HdRenderer {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    static constexpr std::uint64_t kDeadlineNs = 1'000'000'000;
    static constexpr std::size_t kMaxFrameBytes = 4u * 1024u * 1024u;

    static constexpr std::uint64_t kSliceNs = 2'000'000;

    struct Wiring {
        const os::IClock& clock;
        const HdOsdAskCell& ask;
        HdOsdStatusCell& status;

        infra::OptRef<const svc::Vfs> vfs{};
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

    void set_logo(FrontEndLogoKnobs knobs) noexcept { logo_knobs_ = knobs; }

    void begin() noexcept;
    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] infra::ParkTimeout park_ms() const noexcept;
    void release() noexcept;

    [[nodiscard]] std::uint64_t ticks() const noexcept { return ticks_; }

    [[nodiscard]] const HdOsdStatus& status() const noexcept { return st_; }

    [[nodiscard]] BackdropJob::Step job_step() const noexcept { return job_.step(); }

    [[nodiscard]] HdLook look() const noexcept { return look_; }

private:
    struct PanelDrawn {
        infra::CellGen gen{};
        HdBadgeView view{};
        CanvasGen fill{};
    };

    static constexpr std::array<HdDirtyLedger::Region, 2> kCards{
        HdDirtyLedger::Region::Panel,
        HdDirtyLedger::Region::Band
    };

    void publish_() noexcept;
    [[nodiscard]] os::RawMonotonicClock::time_point now_() const noexcept;
    [[nodiscard]] bool ask_moved_() const noexcept;
    [[nodiscard]] bool panel_asked_() const noexcept;
    [[nodiscard]] bool panel_owed_() const noexcept;
    [[nodiscard]] bool canvas_fresh_() const noexcept;
    [[nodiscard]] bool stale_(FlipBuffer buffer) const noexcept;
    [[nodiscard]] bool cards_stale_(FlipBuffer buffer) const noexcept;
    [[nodiscard]] bool spills_(const HdDirty& dirty, const HdLayout& layout) const noexcept;
    [[nodiscard]] std::optional<FlipBuffer> flip_target_(HdFlipHolds::Instant now) const noexcept;
    [[nodiscard]] bool flip_wanted_() const noexcept;
    [[nodiscard]] bool can_progress_(os::RawMonotonicClock::time_point now) const noexcept;
    [[nodiscard]] std::optional<HdFlipHolds::Span> wait_ns_(
        HdFlipHolds::Instant now
    ) const noexcept;
    [[nodiscard]] bool ensure_frame_() noexcept;
    void close_job_() noexcept;
    [[nodiscard]] std::unique_ptr<HdRaster>& raster_() noexcept;
    [[nodiscard]] bool retheme_(HdLook look) noexcept;
    void draw_() noexcept;
    void start_epoch_(const HdLayout& layout) noexcept;

    void paint_canvas_() noexcept;

    void load_wallpaper_() noexcept;
    [[nodiscard]] const FrontEndPattern* pattern_for_(FrontEndBg bg) const noexcept;

    [[nodiscard]] bool card_() const noexcept;

    [[nodiscard]] bool samples_() const noexcept;
    void panel_step_(const HdLayout& layout, PxPlane panel, bool asked) noexcept;
    void place_(HdFlipHolds::Instant at, bool wrote = false) noexcept;

    void handoff_() noexcept;
    [[nodiscard]] PixelCount in_place_px_() const noexcept;
    [[nodiscard]] bool copy_stale_(HdFlipHolds::Instant at) noexcept;
    bool flip_(HdFlipHolds::Instant at) noexcept;
    [[nodiscard]] bool slice_(os::RawMonotonicClock::time_point now) noexcept;
    [[nodiscard]] bool read_(os::RawMonotonicClock::time_point now) noexcept;
    void visible_(os::RawMonotonicClock::time_point now, bool by_flip) noexcept;
    [[nodiscard]] PxPlane frame_plane_() noexcept;
    [[nodiscard]] BackdropJob::Canvas canvas_() noexcept;

    void copy_rect_(FlipBuffer buffer, HdRect rect, bool compose = true) noexcept;

    Wiring w_;
    HdTickPacer pacer_;
    HdOsdAskCell::Reader reader_{};
    HdOsdAsk ask_{};
    bool up_ = false;
    bool raster_open_ = false;
    bool scaler_uio_ = false;
    bool epoch_valid_ = false;
    bool pending_ = false;
    bool deadline_ = false;

    std::optional<PanelDrawn> drawn_{};
    std::uint64_t ticks_ = 0;
    HdEpoch epoch_seen_{};
    FlipSeq seq_{};
    HdOsdStatus st_{};
    HdLook look_{};
    HdTheme theme_ = hd_theme_tokens(HdLook{});

    std::optional<HdFlipHolds> holds_{};
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
    CanvasGen canvas_gen_{};
    CanvasGen fill_gen_{};
    CanvasGen shown_gen_{};
    std::optional<os::RawMonotonicClock::time_point> pending_stamp_{};
    os::RawMonotonicClock::time_point job_started_{};
    os::RawMonotonicClock::time_point gen_started_{};
    std::chrono::nanoseconds scale_{};
    std::chrono::nanoseconds copy_{};

    BandFadePattern<Axis::Horizontal> hbars1_{};
    BandGradientPattern<Axis::Horizontal> hbars2_{};
    BandFadePattern<Axis::Vertical> vbars1_{};
    BandGradientPattern<Axis::Vertical> vbars2_{};
    SpectrumPattern spectrum_{};
    BlackPattern black_{};

    WallpaperPattern wallpaper_{};
    FixedStr<8, StrFit::Clip> wallpaper_label_{};

    FrontEndLogo logo_{};
    FrontEndLogoKnobs logo_knobs_{};

    FrontEndBg bg_seen_ = FrontEndBg::None;
    FrontEndIdle idle_seen_ = FrontEndIdle::Awake;
    HdOsdShows shows_seen_ = HdOsdShows::Menu;
};

}
