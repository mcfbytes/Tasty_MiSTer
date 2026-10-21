// SPDX-License-Identifier: GPL-3.0-or-later
#include "hd_osd_block.h"

#include <memory>

#include "hal/fpga_memory.h"
#include "hal/scaler_buffers.h"
#include "infra/park_fds.h"
#include "report.h"
#ifdef TASTY_HAVE_HDOSD
#include "app/slint_raster.h"
#endif

namespace mister::fw {

namespace {

hal::PhysRegion video_fb_region(std::span<const hal::PhysRegion> regions) noexcept {
    if (regions.size() != hal::kRegionCount)
        return hal::PhysRegion{};
    return regions[static_cast<std::size_t>(hal::RegionId::VideoFb)];
}
}

std::optional<app::HdRenderer::Frame> HdOsdBlock::map_frame_(const BootParts& plat) noexcept {
    std::unique_ptr<app::HdRaster> raster;
#ifdef TASTY_HAVE_HDOSD
    raster = std::make_unique<app::SlintRaster>();
#endif
    auto mem = hal::FpgaMemory::map_aperture(
        video_fb_region(plat.regions),
        os::MmioRegion::Access::ReadWrite
    );
    if (!mem) {
        hd_fb_failed_ = true;
        hd_fb_error_ = mem.error();
        return std::nullopt;
    }
    hd_raster_ = raster != nullptr;
    return app::HdRenderer::Frame{hd_flips_, std::move(*mem), std::move(raster)};
}

HdOsdBlock::HdOsdBlock(
    const BootParts& plat,
    const os::IClock& clock,
    app::HpsFramebuffer& fb,
    const app::VideoPump& video
)
    : hd_renderer_{
          app::HdRenderer::Wiring{.clock = clock, .ask = hd_ask_, .status = hd_status_},
          map_frame_(plat)
      },
      hd_screen_{app::HdOsdScreen::Wiring{
          .ask = hd_ask_,
          .status = hd_status_,
          .flips = hd_flips_,
          .wake = hd_wake_,
          .fb = fb,
          .video = video,
          .clock = clock,
          .vt = infra::OptRef<os::IVtConsole>{hd_vt_console_}
      }} {

    if (auto fds = xthread::ParkFds::create(hd_wake_)) {
        hd_fds_.emplace(std::move(*fds));
    } else {
        hd_fds_error_ = fds.error();
    }
    if (plat.regions.size() == hal::kRegionCount) {
        auto sc = hal::ScalerBuffers::map(
            plat.regions[static_cast<std::size_t>(hal::RegionId::ScalerOut)]
        );
        if (sc) {
            hd_renderer_.bind_scaler(std::move(*sc));
        } else {
            hd_scaler_failed_ = true;
            hd_scaler_error_ = sc.error();
        }
    }
}

bool HdOsdBlock::open_seat(
    const svc::ConfigSnapshot& c,
    ThreadAssembly::SeatMains& mains
) noexcept {

    const bool asked = c.osd_style == 1;
    if (asked && hd_fb_failed_) {
        report("FpgaMemory::map(video-fb) (degraded: classic OSD)", hd_fb_error_);
    }
    if (asked && hd_scaler_failed_) {
        report("ScalerBuffers::map (degraded: HD backdrop absent)", hd_scaler_error_);
    }
    if (!hd_raster_) {
        if (asked)
            report("HD OSD (degraded: classic OSD)", Error{Errc::dt_missing, ERR_SITE(), 0});
        return false;
    }
    hd_renderer_.set_fps(c.hd_osd_fps);
    if (!hd_fds_) {
        report("HdOsdMain::open (degraded: classic OSD)", hd_fds_error_);
        return false;
    }
    hd_main_.emplace(hd_renderer_, std::move(*hd_fds_));
    mains.mains.bind(&*hd_main_);
    return true;
}

infra::OptRef<app::HdOsdScreen> HdOsdBlock::screen() noexcept {
    if (!hd_raster_ || !hd_fds_)
        return infra::OptRef<app::HdOsdScreen>{};
    return infra::OptRef<app::HdOsdScreen>{hd_screen_};
}

}
