// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/osd_wire.h"

namespace mister::app {

unsigned OsdWire::backlog() const noexcept {
    unsigned n = 0;
    const unsigned bound = surface_->visible_rows();
    for (unsigned i = 0; i < bound; ++i) {
        const proto::OsdRow r{static_cast<std::uint8_t>(i)};
        if (surface_->seq(r) != seen_[i]) ++n;
    }
    return n;
}

bool OsdWire::flush_one() noexcept {
    unsigned pending = 0;
    int target = -1;
    std::uint32_t target_seq = 0;

    const unsigned bound = surface_->visible_rows();
    for (unsigned i = 0; i < bound; ++i) {
        const proto::OsdRow r{static_cast<std::uint8_t>(i)};
        const std::uint32_t s = surface_->seq(r);
        if (s == seen_[i]) continue;
        ++pending;
        if (target < 0) {
            target = static_cast<int>(i);
            target_seq = s;
        }
    }
    if (pending > backlog_hw_) backlog_hw_ = pending;
    if (target < 0) return false;
    if (pending > 1) ++starved_;

    const proto::OsdRow row{static_cast<std::uint8_t>(target)};

    const std::uint32_t got = surface_->sample(row, scratch_);
    if (got == 0) {
        ++sample_refusals_;
        return false;
    }
    const auto flushed =
        rows_.flush_row(*link_, row, std::span<const std::uint8_t>(scratch_.b, sizeof scratch_.b));
    if (!flushed) {
        ++flush_failures_;
        return true;
    }

    if (got != target_seq) {
        ++commits_refused_;
        return true;
    }
    seen_[target] = target_seq;
    ++rows_flushed_;
    return true;
}

Ex<void> OsdWire::set_show(proto::LinkOp::OsdShow s, proto::OsdTarget t) noexcept {
    const auto hdmi = static_cast<std::uint8_t>(proto::OsdTarget::Hdmi);
    const bool drop_hdmi = (static_cast<std::uint8_t>(target_) & hdmi) != 0 &&
                           (static_cast<std::uint8_t>(t) & hdmi) == 0;
    if (drop_hdmi) {
        if (auto e = osd_.set_target(proto::OsdTarget::Hdmi); !e) return e;
        if (auto e = osd_.set_visible(*link_, false); !e) return e;
    }
    if (auto e = osd_.set_target(t); !e) return e;
    switch (s) {
        case proto::LinkOp::OsdShow::Off:
            if (auto r = osd_.set_visible(*link_, false); !r) return r;
            show_ = s;
            target_ = t;
            ++vis_writes_;
            return {};
        case proto::LinkOp::OsdShow::Menu:
            if (auto r = osd_.set_visible(*link_, true); !r) return r;
            show_ = s;
            target_ = t;
            ++vis_writes_;
            return {};
        case proto::LinkOp::OsdShow::Overlay:
            if (auto r = osd_.enable(*link_, proto::OsdMode::Message); !r) return r;
            show_ = s;
            target_ = t;
            ++vis_writes_;
            return {};
    }

    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(s)});
}

}  // namespace mister::app
