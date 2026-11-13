// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/osd_grant.h"
#include "app/spi_osd_rows.h"
#include "infra/error.h"
#include "hal/spi_transport.h"
#include "proto/link_op.h"
#include "proto/osd_transport.h"
#include "infra/seat.h"
#include "svc/types.h"

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

    bool flush_one() noexcept {
        if (emit_info_window_())
            return true;
        return route_ == OsdPageRoute::Lw ? lw_rows_.flush_one() : link_rows_.flush_one();
    }

    void bind_rows(OsdPageRoute r) noexcept { route_ = r; }

    [[nodiscard]] OsdPageRoute page_route() const noexcept { return route_; }

    void bind_rotation(proto::OsdRotation rotation) noexcept { rotation_ = rotation; }

    [[nodiscard]] Ex<void> send_rotation() noexcept;

    enum class ShowOwner : std::uint8_t { Policy, Bar };

    [[nodiscard]] Ex<void> set_show(proto::LinkOp::OsdShow s) noexcept {
        return set_show(s, target_);
    }

    [[nodiscard]] Ex<void> set_show(proto::LinkOp::OsdShow s, proto::OsdTarget t) noexcept {
        return show_as(s, ShowOwner::Policy, t);
    }

    [[nodiscard]] Ex<void> show_as(proto::LinkOp::OsdShow s, ShowOwner owner) noexcept {
        return show_as(s, owner, target_);
    }

    [[nodiscard]] Ex<void> show_as(
        proto::LinkOp::OsdShow s,
        ShowOwner owner,
        proto::OsdTarget t
    ) noexcept;

    [[nodiscard]] Ex<void> release(ShowOwner owner) noexcept;

    [[nodiscard]] bool opened_by(ShowOwner owner) const noexcept { return opener_ == owner; }

    [[nodiscard]] Ex<void> reassert_show() noexcept;

    void on_fabric_reprogrammed() noexcept {
        show_ = proto::LinkOp::OsdShow::Off;
        opener_.reset();
        reasserted_ = false;
        asleep_ = false;
    }

    [[nodiscard]] Ex<void> set_visible(bool on) noexcept {
        return set_show(on ? proto::LinkOp::OsdShow::Menu : proto::LinkOp::OsdShow::Off);
    }

    [[nodiscard]] Ex<void> menu_ctl(bool enable) noexcept;

    std::uint32_t rows_flushed() const noexcept { return tally_.rows_flushed; }

    std::uint32_t flush_failures() const noexcept { return tally_.flush_failures; }

    std::uint32_t commits_refused() const noexcept { return tally_.commits_refused; }

    std::uint32_t sample_refusals() const noexcept { return tally_.sample_refusals; }

    std::uint32_t starved() const noexcept { return tally_.starved; }

    std::uint32_t backlog_high_water() const noexcept { return tally_.backlog_hw; }

    std::uint32_t page_rows_sent() const noexcept { return tally_.page_rows_sent; }

    std::uint32_t status_rows_sent() const noexcept { return tally_.status_rows_sent; }

    svc::Tally visibility_writes() const noexcept { return vis_writes_; }

    svc::Tally menu_ctls_sent() const noexcept { return tally_.menu_ctls_sent; }

    bool asleep() const noexcept { return asleep_; }

    svc::Tally info_window_writes() const noexcept { return info_writes_; }

    svc::Tally info_window_failures() const noexcept { return info_failures_; }

    svc::Tally rotations_sent() const noexcept { return tally_.rotations_sent; }

    svc::Tally rotation_failures() const noexcept { return tally_.rotation_failures; }

    proto::LinkOp::OsdShow show() const noexcept { return show_; }

    bool lit() const noexcept { return show_ != proto::LinkOp::OsdShow::Off; }

    bool has_focus() const noexcept { return show_ == proto::LinkOp::OsdShow::Menu; }

    proto::OsdRowCount backlog() const noexcept {
        return route_ == OsdPageRoute::Lw ? lw_rows_.backlog() : link_rows_.backlog();
    }

    proto::OsdSurface& surface() noexcept { return *surface_; }

private:
    bool emit_info_window_() noexcept;

    void latch_lit(proto::LinkOp::OsdShow s, ShowOwner owner, proto::OsdTarget t) noexcept {
        show_ = s;
        target_ = t;
        if (s == proto::LinkOp::OsdShow::Menu) {
            opener_.reset();
        } else if (owner == ShowOwner::Bar && s != proto::LinkOp::OsdShow::Off) {
            opener_ = owner;
        }
        vis_writes_.add();
    }

    proto::OsdSurface* surface_;
    hal::ISpiTransport* link_;
    OsdRowTally tally_{};
    SpiOsdRows<proto::OsdRow{}> link_rows_;
    SpiOsdRows<proto::OsdSurface::kStatusRow> lw_rows_;
    OsdPageRoute route_ = OsdPageRoute::Link;
    proto::OsdTransport osd_{};
    xthread::Telemetry<proto::InfoWindow>::Reader info_seen_{};
    svc::Tally vis_writes_{};
    svc::Tally info_writes_{};
    svc::Tally info_failures_{};
    proto::LinkOp::OsdShow show_ = proto::LinkOp::OsdShow::Off;

    std::optional<ShowOwner> opener_{};
    bool reasserted_ = false;

    bool asleep_ = false;
    proto::OsdTarget target_ = proto::OsdTarget::All;

    proto::OsdRotation rotation_ = proto::OsdRotation::None;
};

}
