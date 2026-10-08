// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "app/fb_view.h"
#include "app/output_geometry.h"
#include "hal/phys_region.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "svc/video_service.h"

namespace mister::app {

class HpsFramebuffer {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::string_view kModeParam = "/sys/module/MiSTer_fb/parameters/mode";

    HpsFramebuffer(svc::VideoService::IVideoWireSink& wire, const IOutputGeometry& output,
                   hal::PhysRegion fb, std::string mode_param = std::string(kModeParam)) noexcept
        : wire_(wire), output_(output), fb_(fb), mode_param_(std::move(mode_param)) {}
    HpsFramebuffer(const HpsFramebuffer&) = delete;
    HpsFramebuffer& operator=(const HpsFramebuffer&) = delete;

    [[nodiscard]] Ex<void> raise() noexcept;

    [[nodiscard]] Ex<void> raise_view(const FbView& view) noexcept;

    [[nodiscard]] Ex<void> drop() noexcept;

    [[nodiscard]] bool take_fb_cmd(std::string_view line) noexcept;

    void hold(bool on) noexcept { held_ = on; }

    [[nodiscard]] bool raised() const noexcept { return raised_; }
    [[nodiscard]] bool held() const noexcept { return held_; }
    [[nodiscard]] std::size_t region_bytes() const noexcept { return fb_.len; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::uint32_t fb_cmds() const noexcept { return fb_cmds_; }
    [[nodiscard]] std::uint32_t refused() const noexcept { return refused_; }

private:
    [[nodiscard]] Ex<void> publish_(std::uint32_t addr, std::uint16_t fmt, std::uint32_t w,
                                    std::uint32_t h, std::uint32_t hmin, std::uint32_t hmax,
                                    std::uint32_t vmin, std::uint32_t vmax,
                                    std::uint32_t stride) noexcept;
    void write_mode_param_(unsigned fmt, unsigned rb, std::uint32_t w, std::uint32_t h,
                           std::uint32_t stride) noexcept;

    svc::VideoService::IVideoWireSink& wire_;
    const IOutputGeometry& output_;
    hal::PhysRegion fb_;
    std::string mode_param_;
    bool raised_ = false;
    bool held_ = false;
    bool viewing_ = false;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t fb_cmds_ = 0;
    std::uint32_t refused_ = 0;
};

}  // namespace mister::app
