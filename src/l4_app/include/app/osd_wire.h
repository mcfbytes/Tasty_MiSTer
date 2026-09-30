// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/error.h"
#include "infra/telemetry.h"
#include "hal/spi_transport.h"
#include "proto/link_op.h"
#include "proto/osd_transport.h"
#include "infra/seat.h"

namespace mister::app {

struct UiPageRecord {
    std::uint32_t depth = 0;
    std::uint32_t top_id = 0;
    std::uint32_t vis_publishes = 0;

    std::uint32_t cell_refusals = 0;

    std::uint32_t page_refusals = 0;
};

using UiPageCell = xthread::Telemetry<UiPageRecord, SeatTag::Ui>;

class OsdWire {
    TASTY_SEAT_MEDIATOR(Ui, RT);

public:
    OsdWire(proto::OsdSurface& surface, hal::ISpiTransport& link) noexcept
        : surface_(&surface), link_(&link) {}

    OsdWire(const OsdWire&) = delete;
    OsdWire& operator=(const OsdWire&) = delete;

    bool flush_one() noexcept;

    [[nodiscard]] Ex<void> set_show(proto::LinkOp::OsdShow s) noexcept;

    void on_fabric_reprogrammed() noexcept { show_ = proto::LinkOp::OsdShow::Off; }

    Ex<void> set_visible(bool on) noexcept {
        return set_show(on ? proto::LinkOp::OsdShow::Menu : proto::LinkOp::OsdShow::Off);
    }

    std::uint32_t rows_flushed() const noexcept { return rows_flushed_; }
    std::uint32_t flush_failures() const noexcept { return flush_failures_; }
    std::uint32_t commits_refused() const noexcept { return commits_refused_; }

    std::uint32_t sample_refusals() const noexcept { return sample_refusals_; }
    std::uint32_t starved() const noexcept { return starved_; }
    std::uint32_t backlog_high_water() const noexcept { return backlog_hw_; }
    std::uint32_t visibility_writes() const noexcept { return vis_writes_; }
    proto::LinkOp::OsdShow show() const noexcept { return show_; }
    bool lit() const noexcept { return show_ != proto::LinkOp::OsdShow::Off; }

    bool has_focus() const noexcept { return show_ == proto::LinkOp::OsdShow::Menu; }

    unsigned backlog() const noexcept;

    proto::OsdSurface& surface() noexcept { return *surface_; }

private:
    proto::OsdSurface* surface_;
    hal::ISpiTransport* link_;
    proto::OsdTransport osd_{};
    proto::OsdSurface::Row scratch_{};

    std::uint32_t seen_[proto::OsdSurface::kMaxRows]{};
    std::uint32_t rows_flushed_ = 0;
    std::uint32_t flush_failures_ = 0;
    std::uint32_t commits_refused_ = 0;
    std::uint32_t sample_refusals_ = 0;
    std::uint32_t starved_ = 0;
    std::uint32_t backlog_hw_ = 0;
    std::uint32_t vis_writes_ = 0;
    proto::LinkOp::OsdShow show_ = proto::LinkOp::OsdShow::Off;
};

}  // namespace mister::app
