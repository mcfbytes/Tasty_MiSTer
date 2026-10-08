// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "app/hd_raster.h"

namespace mister::app {

class SlintRaster final : public HdRaster {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    SlintRaster();
    ~SlintRaster() override;

    SlintRaster(const SlintRaster&) = delete;
    SlintRaster& operator=(const SlintRaster&) = delete;

private:
    bool open_(const HdTheme& theme, const HdLayout& layout) override;
    HdRect render_(const PageDescription& page, const HdBadgeView& badge,
                   std::span<std::uint16_t> panel, std::uint32_t stride_px) override;
    void close_() override;
    void relayout_(const HdLayout& layout) override;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mister::app
