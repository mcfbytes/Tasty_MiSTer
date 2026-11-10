// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/hdmi_link.h"

#include "svc/adv7513_bus.h"

#include "hal/hdmi_int.h"

#include "video_service_detail.h"

namespace mister::svc {

HdmiLink::HdmiLink(adv7513::Io io) noexcept : io_(io) {}

Ex<void> HdmiLink::poll_hotplug(adv7513::Adv7513Bus& bus) {

    if (!hdmi_capable_)
        return {};

    if (auto c = cec_.poll_deadline(io_); !c) {
        return err(c.error().code, ERR_SITE(), c.error().detail);
    }

    if (!io_.hdmi->hdmi_int_asserted())
        return {};

    auto irq = bus.read_interrupts();

    if (!irq)
        return err(irq.error().code, ERR_SITE(), irq.error().detail);
    if (irq->none())
        return {};

    const adv7513::IntBits clear_mask = irq->pending_hotplug();
    if (clear_mask.none())
        return {};
    if (auto r = bus.clear_interrupts(clear_mask); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }

    auto status = bus.read_status();
    if (!status)
        return err(status.error().code, ERR_SITE(), status.error().detail);

    if (status->sink_present()) {

        init_owed_ = true;
        if (!power_requested_)
            return {};
        if (auto r = bus.tmds_power(true); !r)
            return r;
        sink_powered_ = true;
        return settle_and_publish(bus);
    }

    if (auto r = bus.tmds_power(false); !r)
        return r;
    sink_powered_ = false;
    return {};
}

Ex<void> HdmiLink::settle_and_publish(adv7513::Adv7513Bus& bus) {
    auto st = bus.read_status();
    if (!st)
        return err(st.error().code, ERR_SITE(), st.error().detail);
    if (!st->sink_present())
        return {};
    ++hpd_gen_.v;
    return {};
}

Ex<void> HdmiLink::request_power(adv7513::Adv7513Bus& bus, bool on) {
    power_requested_ = on;
    if (auto r = bus.tmds_power(on); !r)
        return r;
    sink_powered_ = on;
    if (!on)
        return {};

    if (init_owed_)
        return settle_and_publish(bus);
    ++audio_gen_.v;
    return {};
}

}
