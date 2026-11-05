// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app/osd_row_tally.h"
#include "hal/spi_transport.h"
#include "infra/seat.h"
#include "proto/osd_surface.h"
#include "proto/osd_transport.h"

namespace mister::app {

template <proto::OsdRow First>
class SpiOsdRows final {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr proto::OsdRow kFirstRow = First;
    static_assert(
        First == proto::OsdRow{} || First == proto::OsdSurface::kStatusRow,
        "the link's whole range, or the status rows past the page"
    );

    [[nodiscard]] static constexpr bool reaches(proto::OsdRow row) noexcept {
        return row >= First && proto::OsdSurface::kMaxRows.covers(row);
    }

    SpiOsdRows(
        const proto::OsdSurface& surface,
        hal::ISpiTransport& link,
        OsdRowTally& tally
    ) noexcept
        : surface_(&surface),
          link_(&link),
          tally_(&tally) {}

    SpiOsdRows(const SpiOsdRows&) = delete;
    SpiOsdRows& operator=(const SpiOsdRows&) = delete;

    bool flush_one() noexcept;
    [[nodiscard]] proto::OsdRowCount backlog() const noexcept;

private:
    const proto::OsdSurface* surface_;
    hal::ISpiTransport* link_;
    OsdRowTally* tally_;
    proto::OsdTransport rows_{};
    proto::OsdSurface::Row scratch_{};

    proto::OsdRowCells<std::uint32_t, proto::OsdSurface::kMaxRows.size()> seen_{};
};

extern template class SpiOsdRows<proto::OsdRow{}>;
extern template class SpiOsdRows<proto::OsdSurface::kStatusRow>;

static_assert(
    SpiOsdRows<proto::OsdSurface::kStatusRow>::kFirstRow == proto::OsdSurface::kPageRows.end()
);
static_assert(
    !SpiOsdRows<proto::OsdSurface::kStatusRow>::reaches(
        proto::OsdSurface::kPageRows.end() - proto::kOneOsdRow
    ),
    "a bridge core's sink cannot form a page-row word"
);

}
