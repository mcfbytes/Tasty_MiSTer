// SPDX-License-Identifier: GPL-3.0-or-later
#include "hd_osd_block.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>

#include "app/hd/front_end_logo.h"
#include "app/hd/hd_look_ini.h"
#include "hal/fpga_memory.h"
#include "hal/board_regions.h"
#include "hal/scaler_buffers.h"
#include "infra/xthread/park_fds.h"
#include "infra/prelude.h"
#include "app/hd/slint_raster.h"

namespace mister::fw {

namespace {

constexpr std::array<app::hd::LogoRotation, 3> kLogoRotationByIni{
    app::hd::LogoRotation::Upright,
    app::hd::LogoRotation::Left,
    app::hd::LogoRotation::Right
};

}

std::optional<app::hd::HdRenderer::Frame> HdOsdBlock::map_frame_(
    const BootParts& plat,
    OpenVerdict& verdict
) noexcept {
    app::hd::HdRasters rasters = app::hd::make_hd_rasters();
    if (!std::ranges::all_of(rasters, [](const auto& r) { return r != nullptr; })) {
        return std::nullopt;
    }
    const auto region = hal::BoardRegions::aperture_region(plat.regions, hal::RegionId::VideoFb);
    if (!region)
        return std::nullopt;
    auto mem = hal::FpgaMemory::map_scanout(
        plat.aperture_node,
        *region,
        plat.write_combine,
        plat.video.vsync_device
    );
    if (!mem) {
        verdict.refuse("T-HDOSD: FpgaMemory::map(video-fb)", mem.error());
        return std::nullopt;
    }
    std::fprintf(
        stderr,
        "mister: T-HDOSD slots mapped via %s\n",
        hal::mapping_name(mem->mapping())
    );
    hd_raster_ = true;
    return app::hd::HdRenderer::Frame{hd_flips_, std::move(*mem), std::move(rasters)};
}

HdOsdBlock::HdOsdBlock(
    const BootParts& plat,
    const os::IClock& clock,
    app::HpsFramebuffer& fb,
    const app::VideoPump& video,
    OpenVerdict& verdict
)
    : hd_renderer_{
          app::hd::HdRenderer::Wiring{
              .clock = clock,
              .ask = hd_ask_,
              .status = hd_status_,
              .vfs = infra::OptRef<const svc::Vfs>{plat.vfs}
          },
          map_frame_(plat, verdict)
      },
      hd_screen_{app::hd::HdOsdScreen::Wiring{
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
    if (const auto scaler = hal::BoardRegions::aperture_region(
            plat.regions,
            hal::RegionId::ScalerOut
        )) {
        if (auto sc = hal::ScalerBuffers::map(plat.fpga_mem, plat.aperture_node, *scaler); sc) {
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
    hd_renderer_.set_look(app::hd::hd_look_from_ini(c));
    const app::hd::LogoRotation rotation = c.osd_rotate < kLogoRotationByIni.size()
        ? kLogoRotationByIni[c.osd_rotate]
        : app::hd::LogoRotation::Upright;
    hd_renderer_.set_logo(
        app::hd::FrontEndLogoKnobs{
            .show = c.logo != 0,
            .rotation = rotation,
            .direct_video = svc::direct_video_resolved(c) != svc::DirectVideo::Off
        }
    );
    hd_main_.emplace(hd_renderer_, std::move(seated(hd_fds_, "T-HDOSD bound past a refused open")));
    mains.bind(&*hd_main_);
    return true;
}

infra::OptRef<app::hd::HdOsdScreen> HdOsdBlock::screen() noexcept {
    if (!hd_raster_ || !hd_fds_)
        return infra::OptRef<app::hd::HdOsdScreen>{};
    return infra::OptRef<app::hd::HdOsdScreen>{hd_screen_};
}

}
