// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "app/fb_tenancy.h"
#include "app/fb_view.h"
#include "app/hd_rect.h"
#include "app/output_geometry.h"
#include "app/picture_size.h"
#include "hal/phys_region.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "svc/video/video_service.h"

namespace mister::app {

struct FbScanWindow {
    HdRect box{};

    [[nodiscard]] static constexpr FbScanWindow whole(PictureSize out) noexcept {
        return FbScanWindow{HdRect{0, 0, out.width.v, out.rows.last_index() + 1}};
    }

    [[nodiscard]] static FbScanWindow centred(const FbView& picture, PictureSize out) noexcept;
};

[[nodiscard]] constexpr std::array<hal::SpiWord, 4> fb_scan_words(
    const FbScanWindow& window
) noexcept {
    const HdRect& b = window.box;
    return {
        hal::SpiWord{static_cast<std::uint16_t>(b.x)},
        hal::SpiWord{static_cast<std::uint16_t>(b.x + b.w - 1)},
        hal::SpiWord{static_cast<std::uint16_t>(b.y)},
        hal::SpiWord{static_cast<std::uint16_t>(b.y + b.h - 1)}
    };
}

class HpsFramebuffer {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::string_view kModeParam = "/sys/module/MiSTer_fb/parameters/mode";

    HpsFramebuffer(
        svc::video::VideoService::IVideoWireSink& wire,
        const IOutputGeometry& output,
        hal::PhysRegion fb,
        std::string mode_param = std::string(kModeParam)
    ) noexcept
        : wire_(wire),
          output_(output),
          fb_(fb),
          mode_param_(std::move(mode_param)) {}

    HpsFramebuffer(const HpsFramebuffer&) = delete;
    HpsFramebuffer& operator=(const HpsFramebuffer&) = delete;

    [[nodiscard]] Ex<FbLease> raise(FbTenant who) noexcept;

    [[nodiscard]] Ex<FbLease> raise_view(FbTenant who, const FbView& view) noexcept;

    [[nodiscard]] Ex<void> drop(const FbLease& lease) noexcept;

    [[nodiscard]] bool take_fb_cmd(std::string_view line) noexcept;

    void hold(bool on) noexcept { tenancy_.hold(on); }

    [[nodiscard]] bool raised() const noexcept { return tenancy_.claimed(); }

    [[nodiscard]] bool held() const noexcept { return tenancy_.held(); }

    [[nodiscard]] bool holds(const FbLease& lease) const noexcept { return tenancy_.holds(lease); }

    [[nodiscard]] const FbView& picture() const noexcept { return picture_; }

    [[nodiscard]] std::size_t region_bytes() const noexcept { return fb_.len; }

    [[nodiscard]] std::uint32_t refused() const noexcept { return refused_; }

private:
    [[nodiscard]] Ex<void> publish_(const FbView& picture, FbScanWindow window) noexcept;

    void write_mode_param_(const FbView& picture) noexcept;

    [[nodiscard]] PictureSize output_size_() const noexcept {
        return PictureSize::of(output_.output_size());
    }

    svc::video::VideoService::IVideoWireSink& wire_;
    const IOutputGeometry& output_;
    hal::PhysRegion fb_;
    std::string mode_param_;
    FbView picture_{};
    FbTenancy tenancy_{};
    std::uint32_t refused_ = 0;
};

}
