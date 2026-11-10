// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/osd_wire.h"

#include "proto/link.h"

namespace mister::app {

Ex<void> OsdWire::show_as(proto::LinkOp::OsdShow s, ShowOwner owner, proto::OsdTarget t) noexcept {
    proto::Link link{*link_};
    const bool drop_hdmi = proto::includes_hdmi(target_) && !proto::includes_hdmi(t);
    if (drop_hdmi) {
        if (auto e = osd_.set_target(proto::OsdTarget::Hdmi); !e)
            return e;
        if (auto e = osd_.set_visible(link, false); !e)
            return e;
    }
    if (auto e = osd_.set_target(t); !e)
        return e;
    switch (s) {
        case proto::LinkOp::OsdShow::Off:
            if (auto r = osd_.set_visible(link, false); !r)
                return r;
            latch_lit(s, owner, t);
            return {};
        case proto::LinkOp::OsdShow::Menu:
            if (auto r = osd_.set_visible(link, true); !r)
                return r;
            latch_lit(s, owner, t);
            return {};
        case proto::LinkOp::OsdShow::Overlay: {

            if (owner == ShowOwner::Policy) {
                std::optional<proto::InfoWindow> window = info_seen_.take_if_changed(
                    surface_->info_window_cell()
                );
                if (!window) {
                    const auto sampled = surface_->info_window_cell().sample();
                    if (sampled)
                        window = sampled.value;
                }
                if (window && !window->empty()) {
                    if (auto r = osd_.enable_info(link, *window); !r) {
                        info_failures_.add();
                        info_seen_.forget();
                        return r;
                    }
                    info_writes_.add();
                    latch_lit(s, owner, t);
                    return {};
                }
            }
            if (auto r = osd_.enable(link, proto::OsdMode::Message); !r)
                return r;
            latch_lit(s, owner, t);
            return {};
        }
    }

    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(s)});
}

Ex<void> OsdWire::release(ShowOwner owner) noexcept {
    if (!opened_by(owner))
        return {};
    opener_.reset();
    return show_as(proto::LinkOp::OsdShow::Off, ShowOwner::Policy, target_);
}

Ex<void> OsdWire::reassert_show() noexcept {
    if (!reasserted_ && lit()) {

        if (auto r = show_as(show_, ShowOwner::Policy, target_); !r)
            return r;
    }
    reasserted_ = true;
    return {};
}

bool OsdWire::emit_info_window_() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(OsdWire, B);
    const std::optional<proto::InfoWindow> w = info_seen_.take_if_changed(
        surface_->info_window_cell()
    );
    if (!w)
        return false;
    if (w->empty()) {

        if (!lit())
            return false;
        if (auto r = show_as(show_, ShowOwner::Policy, target_); !r) {
            info_failures_.add();
            info_seen_.forget();
            return true;
        }
        return true;
    }
    proto::Link link{*link_};
    if (auto sent = osd_.enable_info(link, *w); !sent) {
        info_failures_.add();
        info_seen_.forget();
        return true;
    }
    info_writes_.add();
    return true;
}

}
