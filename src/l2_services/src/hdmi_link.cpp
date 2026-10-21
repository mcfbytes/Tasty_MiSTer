// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/hdmi_link.h"

#include "svc/adv7513_reg_map.h"
#include "svc/adv7513_reg_write.h"

#include "hal/hdmi_int.h"

#include "video_service_detail.h"

namespace mister::svc {

HdmiLink::HdmiLink(adv7513::Io io) noexcept : io_(io) {}

Ex<void> HdmiLink::poll_hotplug() {

    if (!attached_) {
        return err(Errc::negotiation, ERR_SITE());
    }

    if (!hdmi_capable_)
        return {};

    if (auto c = cec_.poll_deadline(); !c) {
        return err(c.error().code, ERR_SITE(), c.error().detail);
    }

    if (!io_.hdmi->hdmi_int_asserted())
        return {};

    auto irq = io_.main->read(adv7513::kRegIntStatus);

    if (!irq)
        return err(irq.error().code, ERR_SITE(), irq.error().detail);
    if (*irq == 0)
        return {};

    const std::uint8_t clear_mask = static_cast<std::uint8_t>(*irq & 0xC0u);
    if (clear_mask == 0)
        return {};
    if (auto r = io_.main->write(adv7513::kRegIntStatus, clear_mask); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }

    auto status = io_.main->read(adv7513::kRegStatus);
    if (!status)
        return err(status.error().code, ERR_SITE(), status.error().detail);
    const bool hpd_high = (*status & 0x40u) != 0;
    const bool ms_high = (*status & 0x20u) != 0;

    if (hpd_high && ms_high) {

        init_owed_ = true;
        if (!power_requested_)
            return {};
        if (auto r = apply_tmds_power(true); !r)
            return r;
        return settle_and_publish();
    }

    return apply_tmds_power(false);
}

Ex<void> HdmiLink::apply_tmds_power(bool on) {
    const adv7513::RegWrite w = adv7513::power(on);
    if (auto r = io_.main->write(w.reg, w.val); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    sink_powered_ = on;
    return {};
}

Ex<void> HdmiLink::settle_and_publish() {
    auto st = io_.main->read(adv7513::kRegStatus);
    if (!st)
        return err(st.error().code, ERR_SITE(), st.error().detail);
    if ((*st & 0x40u) == 0 || (*st & 0x20u) == 0)
        return {};
    ++hpd_gen_.v;
    return {};
}

Ex<void> HdmiLink::request_power(bool on) {
    if (!attached_) {
        return err(Errc::negotiation, ERR_SITE());
    }
    power_requested_ = on;
    if (auto r = apply_tmds_power(on); !r)
        return r;
    if (!on)
        return {};

    if (init_owed_)
        return settle_and_publish();
    ++audio_gen_.v;
    return {};
}

}
