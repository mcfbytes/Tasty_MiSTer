// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "app/hd_raster.h"
#include "app/hd_template_traits.h"

namespace mister::app {

template <class C>
class SlintRasterOf final : public HdRaster {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    explicit SlintRasterOf(const HdTemplateTraits& traits);
    ~SlintRasterOf() override;

    SlintRasterOf(const SlintRasterOf&) = delete;
    SlintRasterOf& operator=(const SlintRasterOf&) = delete;

private:
    bool open_(const HdTheme& theme, const HdLayout& layout) override;
    HdDirty render_(const PageDescription& page, const HdBadgeView& badge, PxPlane panel) override;
    void close_() override;
    void relayout_(const HdLayout& layout) override;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::unique_ptr<HdRaster> make_hd_raster(HdTemplateId id);
[[nodiscard]] HdRasters make_hd_rasters();

}
