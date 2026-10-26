// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "app/link_osd_rows.h"
#include "app/osd_row_tally.h"
#include "hal/spi_transport.h"
#include "infra/seat.h"
#include "proto/osd_surface.h"
#include "proto/osd_transport.h"

namespace mister::app {

template <unsigned First>
class SpiOsdRows final : public ILinkOsdRows {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr unsigned kFirstRow = First;
    static constexpr unsigned kRows = proto::OsdSurface::kMaxRows - First;
    static_assert(
        First == 0 || First == proto::OsdSurface::kPageRows,
        "the link's whole range, or the status rows past the page"
    );

    [[nodiscard]] static constexpr bool reaches(unsigned row) noexcept {
        return row >= First && row < proto::OsdSurface::kMaxRows;
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

    bool flush_one() noexcept override;
    [[nodiscard]] unsigned backlog() const noexcept override;

private:
    const proto::OsdSurface* surface_;
    hal::ISpiTransport* link_;
    OsdRowTally* tally_;
    proto::OsdTransport rows_{};
    proto::OsdSurface::Row scratch_{};

    std::array<std::uint32_t, kRows> seen_{};
};

extern template class SpiOsdRows<0>;
extern template class SpiOsdRows<proto::OsdSurface::kPageRows>;

static_assert(SpiOsdRows<proto::OsdSurface::kPageRows>::kFirstRow == 16);
static_assert(
    !SpiOsdRows<proto::OsdSurface::kPageRows>::reaches(proto::OsdSurface::kPageRows - 1),
    "a bridge core's sink cannot form a page-row word"
);

}
