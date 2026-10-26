// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/osd_wire.h"

namespace mister::app {

Ex<void> OsdWire::set_show(proto::LinkOp::OsdShow s, proto::OsdTarget t) noexcept {
    const auto hdmi = static_cast<std::uint8_t>(proto::OsdTarget::Hdmi);
    const bool drop_hdmi = (static_cast<std::uint8_t>(target_) & hdmi) != 0
        && (static_cast<std::uint8_t>(t) & hdmi) == 0;
    if (drop_hdmi) {
        if (auto e = osd_.set_target(proto::OsdTarget::Hdmi); !e)
            return e;
        if (auto e = osd_.set_visible(*link_, false); !e)
            return e;
    }
    if (auto e = osd_.set_target(t); !e)
        return e;
    switch (s) {
        case proto::LinkOp::OsdShow::Off:
            if (auto r = osd_.set_visible(*link_, false); !r)
                return r;
            show_ = s;
            target_ = t;
            ++vis_writes_;
            return {};
        case proto::LinkOp::OsdShow::Menu:
            if (auto r = osd_.set_visible(*link_, true); !r)
                return r;
            show_ = s;
            target_ = t;
            ++vis_writes_;
            return {};
        case proto::LinkOp::OsdShow::Overlay:
            if (auto r = osd_.enable(*link_, proto::OsdMode::Message); !r)
                return r;
            show_ = s;
            target_ = t;
            ++vis_writes_;
            return {};
    }

    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(s)});
}

}
