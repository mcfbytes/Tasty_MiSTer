// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/osd_grant.h"
#include "app/osd_rows.h"
#include "app/ui_page_record.h"
#include "infra/error.h"
#include "hal/spi_transport.h"
#include "proto/link_op.h"
#include "proto/osd_transport.h"
#include "infra/seat.h"

namespace mister::app {

class OsdWire {
    TASTY_SEAT_MEDIATOR(Ui, RT);

public:
    OsdWire(proto::OsdSurface& surface, hal::ISpiTransport& link) noexcept
        : surface_(&surface),
          link_(&link),
          link_rows_(surface, link, tally_),
          lw_rows_(surface, link, tally_) {}

    OsdWire(const OsdWire&) = delete;
    OsdWire& operator=(const OsdWire&) = delete;

    bool flush_one() noexcept { return rows_->flush_one(); }

    void bind_rows(OsdPageRoute r) noexcept {
        route_ = r;
        rows_ = r == OsdPageRoute::Lw ? static_cast<ILinkOsdRows*>(&lw_rows_) : &link_rows_;
    }

    [[nodiscard]] OsdPageRoute page_route() const noexcept { return route_; }

    [[nodiscard]] Ex<void> set_show(proto::LinkOp::OsdShow s) noexcept {
        return set_show(s, target_);
    }

    [[nodiscard]] Ex<void> set_show(proto::LinkOp::OsdShow s, proto::OsdTarget t) noexcept;

    void on_fabric_reprogrammed() noexcept { show_ = proto::LinkOp::OsdShow::Off; }

    [[nodiscard]] Ex<void> set_visible(bool on) noexcept {
        return set_show(on ? proto::LinkOp::OsdShow::Menu : proto::LinkOp::OsdShow::Off);
    }

    std::uint32_t rows_flushed() const noexcept { return tally_.rows_flushed; }

    std::uint32_t flush_failures() const noexcept { return tally_.flush_failures; }

    std::uint32_t commits_refused() const noexcept { return tally_.commits_refused; }

    std::uint32_t sample_refusals() const noexcept { return tally_.sample_refusals; }

    std::uint32_t starved() const noexcept { return tally_.starved; }

    std::uint32_t backlog_high_water() const noexcept { return tally_.backlog_hw; }

    std::uint32_t page_rows_sent() const noexcept { return tally_.page_rows_sent; }

    std::uint32_t status_rows_sent() const noexcept { return tally_.status_rows_sent; }

    std::uint32_t visibility_writes() const noexcept { return vis_writes_; }

    proto::LinkOp::OsdShow show() const noexcept { return show_; }

    bool lit() const noexcept { return show_ != proto::LinkOp::OsdShow::Off; }

    bool has_focus() const noexcept { return show_ == proto::LinkOp::OsdShow::Menu; }

    unsigned backlog() const noexcept { return rows_->backlog(); }

    proto::OsdSurface& surface() noexcept { return *surface_; }

private:
    proto::OsdSurface* surface_;
    hal::ISpiTransport* link_;
    OsdRowTally tally_{};
    SpiOsdRows<0> link_rows_;
    SpiOsdRows<proto::OsdSurface::kPageRows> lw_rows_;
    ILinkOsdRows* rows_ = &link_rows_;
    OsdPageRoute route_ = OsdPageRoute::Link;
    proto::OsdTransport osd_{};
    std::uint32_t vis_writes_ = 0;
    proto::LinkOp::OsdShow show_ = proto::LinkOp::OsdShow::Off;
    proto::OsdTarget target_ = proto::OsdTarget::All;
};

}
