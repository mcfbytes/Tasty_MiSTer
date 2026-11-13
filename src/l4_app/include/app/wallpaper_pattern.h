// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/checkers_pattern.h"
#include "app/front_end_pattern.h"
#include "app/hd_rect.h"
#include "app/output_pixels.h"
#include "app/output_size.h"
#include "app/px_plane.h"
#include "app/types.h"
#include "infra/seat.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class WallpaperPattern final : public FrontEndPattern {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    void load(const svc::Vfs& vfs, std::string_view ini_label);

    [[nodiscard]] std::string_view path() const noexcept { return path_; }

    [[nodiscard]] bool has_image() const noexcept { return !w_.empty() && !h_.empty(); }

    [[nodiscard]] OutputSize size() const noexcept { return OutputSize{w_, h_}; }

    void paint(PxPlane& canvas, HdRect box) const noexcept override;

private:
    void clear_() noexcept;
    void decode_(std::span<const std::byte> bytes);

    CheckersPattern fallback_{};
    std::string path_{};

    std::vector<Rgb888> colors_{};
    OutputPixels w_{};
    OutputPixels h_{};
};

}
