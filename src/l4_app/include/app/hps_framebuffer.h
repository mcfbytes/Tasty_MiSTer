// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "app/fb_view.h"
#include "app/hd_rect.h"
#include "app/output_geometry.h"
#include "app/picture_size.h"
#include "hal/phys_region.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "svc/video_service.h"

namespace mister::app {

struct FbScanWindow {
    HdRect box{};

    [[nodiscard]] static constexpr FbScanWindow whole(PictureSize out) noexcept {
        return FbScanWindow{HdRect{0, 0, out.width.v, out.rows.last_index() + 1}};
    }

    [[nodiscard]] static FbScanWindow centred(const FbView& picture, PictureSize out) noexcept;
};

[[nodiscard]] constexpr std::array<std::uint16_t, 4> fb_scan_words(
    const FbScanWindow& window
) noexcept {
    const HdRect& b = window.box;
    return {
        static_cast<std::uint16_t>(b.x),
        static_cast<std::uint16_t>(b.x + b.w - 1),
        static_cast<std::uint16_t>(b.y),
        static_cast<std::uint16_t>(b.y + b.h - 1)
    };
}

class HpsFramebuffer {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::string_view kModeParam = "/sys/module/MiSTer_fb/parameters/mode";

    HpsFramebuffer(
        svc::VideoService::IVideoWireSink& wire,
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

    [[nodiscard]] Ex<void> raise() noexcept;

    [[nodiscard]] Ex<void> raise_view(const FbView& view) noexcept;

    [[nodiscard]] Ex<void> drop() noexcept;

    [[nodiscard]] bool take_fb_cmd(std::string_view line) noexcept;

    void hold(bool on) noexcept { held_ = on; }

    [[nodiscard]] bool raised() const noexcept { return raised_; }

    [[nodiscard]] bool held() const noexcept { return held_; }

    [[nodiscard]] const FbView& picture() const noexcept { return picture_; }

    [[nodiscard]] std::size_t region_bytes() const noexcept { return fb_.len; }

    [[nodiscard]] std::uint32_t refused() const noexcept { return refused_; }

private:
    [[nodiscard]] Ex<void> publish_(const FbView& picture, FbScanWindow window) noexcept;

    void write_mode_param_(const FbView& picture) noexcept;

    [[nodiscard]] PictureSize output_size_() const noexcept {
        return PictureSize::of(output_.output_size());
    }

    svc::VideoService::IVideoWireSink& wire_;
    const IOutputGeometry& output_;
    hal::PhysRegion fb_;
    std::string mode_param_;
    FbView picture_{};
    bool raised_ = false;
    bool held_ = false;
    bool viewing_ = false;
    std::uint32_t refused_ = 0;
};

}
