// SPDX-License-Identifier: GPL-3.0-or-later
#include "hd_osd_block.h"

#include <algorithm>
#include <array>
#include <memory>

#include "app/front_end_logo.h"
#include "app/hd_look_ini.h"
#include "hal/fpga_memory.h"
#include "hal/scaler_buffers.h"
#include "infra/park_fds.h"
#ifdef TASTY_HAVE_HDOSD
#include "app/slint_raster.h"
#endif

namespace mister::fw {

namespace {

constexpr std::array<app::LogoRotation, 3> kLogoRotationByIni{
    app::LogoRotation::Upright,
    app::LogoRotation::Left,
    app::LogoRotation::Right
};

}

std::optional<app::HdRenderer::Frame> HdOsdBlock::map_frame_(
    const BootParts& plat,
    OpenVerdict& verdict
) noexcept {
    app::HdRasters rasters{};
#ifdef TASTY_HAVE_HDOSD
    rasters = app::make_hd_rasters();
#endif
    if (!std::ranges::all_of(rasters, [](const auto& r) { return r != nullptr; })) {
        return std::nullopt;
    }
    const auto region = hal::declared_region(plat.regions, hal::RegionId::VideoFb);
    if (!region)
        return std::nullopt;
    auto mem = hal::FpgaMemory::map_aperture(*region, os::MmioRegion::Access::ReadWrite);
    if (!mem) {
        verdict.refuse("T-HDOSD: FpgaMemory::map(video-fb)", mem.error());
        return std::nullopt;
    }
    hd_raster_ = true;
    return app::HdRenderer::Frame{hd_flips_, std::move(*mem), std::move(rasters)};
}

HdOsdBlock::HdOsdBlock(
    const BootParts& plat,
    const os::IClock& clock,
    app::HpsFramebuffer& fb,
    const app::VideoPump& video,
    OpenVerdict& verdict
)
    : hd_renderer_{
          app::HdRenderer::Wiring{
              .clock = clock,
              .ask = hd_ask_,
              .status = hd_status_,
              .vfs = infra::OptRef<const svc::Vfs>{plat.vfs}
          },
          map_frame_(plat, verdict)
      },
      hd_screen_{app::HdOsdScreen::Wiring{
          .ask = hd_ask_,
          .status = hd_status_,
          .flips = hd_flips_,
          .wake = hd_wake_,
          .fb = fb,
          .output = video,
          .stats = video.stats_cell(),
          .clock = clock,
          .vt = infra::OptRef<os::IVtMode>{hd_vt_console_}
      }} {

    if (!hd_raster_)
        return;
    if (auto fds = xthread::ParkFds::create(hd_wake_)) {
        hd_fds_.emplace(std::move(*fds));
    } else {
        verdict.refuse("T-HDOSD: ParkFds::create", fds.error());
    }
    if (const auto scaler = hal::declared_region(plat.regions, hal::RegionId::ScalerOut)) {
        if (auto sc = hal::ScalerBuffers::map(*scaler); sc) {
            hd_renderer_.bind_scaler(std::move(*sc));
        } else {
            verdict.refuse("T-HDOSD: ScalerBuffers::map", sc.error());
        }
    }
}

bool HdOsdBlock::open_seat(
    const svc::ConfigSnapshot& c,
    ThreadAssembly::SeatMains& mains
) noexcept {
    if (!hd_raster_)
        return false;
    hd_renderer_.set_fps(c.hd_osd_fps);
    hd_renderer_.set_look(app::hd_look_from_ini(c));
    const app::LogoRotation rotation = c.osd_rotate < kLogoRotationByIni.size()
        ? kLogoRotationByIni[c.osd_rotate]
        : app::LogoRotation::Upright;
    hd_renderer_.set_logo(
        app::FrontEndLogoKnobs{
            .show = c.logo != 0,
            .rotation = rotation,
            .direct_video = svc::direct_video_resolved(c) != svc::DirectVideo::Off
        }
    );
    hd_main_.emplace(hd_renderer_, std::move(seated(hd_fds_, "T-HDOSD bound past a refused open")));
    mains.bind(&*hd_main_);
    return true;
}

infra::OptRef<app::HdOsdScreen> HdOsdBlock::screen() noexcept {
    if (!hd_raster_ || !hd_fds_)
        return infra::OptRef<app::HdOsdScreen>{};
    return infra::OptRef<app::HdOsdScreen>{hd_screen_};
}

}
