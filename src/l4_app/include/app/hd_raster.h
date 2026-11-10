// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <memory>

#include "app/hd_badge_view.h"
#include "app/hd_dirty.h"
#include "app/hd_layout.h"
#include "app/hd_rect.h"
#include "app/hd_template_traits.h"
#include "app/hd_theme.h"
#include "app/page_description.h"
#include "app/px_plane.h"
#include "infra/seat.h"

namespace mister::app {

class HdRaster {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    virtual ~HdRaster() = default;

    bool open(const HdTheme& theme, const HdLayout& layout) {
        TASTY_SEAT_BODY(HdRaster);
        return open_(theme, layout);
    }

    HdDirty render(const PageDescription& page, const HdBadgeView& badge, PxPlane panel) {
        TASTY_SEAT_BODY(HdRaster);
        return render_(page, badge, panel);
    }

    void close() {
        TASTY_SEAT_BODY(HdRaster);
        close_();
    }

    void relayout(const HdLayout& layout) {
        TASTY_SEAT_BODY(HdRaster);
        relayout_(layout);
    }

private:
    virtual bool open_(const HdTheme& theme, const HdLayout& layout) = 0;
    virtual HdDirty render_(
        const PageDescription& page,
        const HdBadgeView& badge,
        PxPlane panel
    ) = 0;
    virtual void close_() = 0;
    virtual void relayout_(const HdLayout& layout) = 0;
};

using HdRasters = std::array<std::unique_ptr<HdRaster>, kHdTemplateCount>;

}
