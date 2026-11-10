// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/adv7513_io.h"
#include "svc/cec_state_machine.h"
#include "svc/edid_store.h"
#include "svc/types.h"

namespace mister::svc::adv7513 {
class Adv7513Bus;
}

namespace mister::svc {

class HdmiLink {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit HdmiLink(adv7513::Io io) noexcept;
    HdmiLink(const HdmiLink&) = delete;
    HdmiLink& operator=(const HdmiLink&) = delete;

    void set_hdmi_capable(bool v) noexcept { hdmi_capable_ = v; }

    bool hdmi_capable() const noexcept { return hdmi_capable_; }

    [[nodiscard]] Ex<void> poll_hotplug(adv7513::Adv7513Bus& bus);

    [[nodiscard]] Ex<void> request_power(adv7513::Adv7513Bus& bus, bool on);

    bool power_requested() const noexcept { return power_requested_; }

    bool sink_powered() const noexcept { return sink_powered_; }

    bool init_owed() const noexcept { return init_owed_; }

    void clear_init_owed() noexcept { init_owed_ = false; }

    Generation audio_generation() const noexcept { return audio_gen_; }

    Generation hotplug_generation() const noexcept { return hpd_gen_; }

    [[nodiscard]] Ex<void> refresh_edid() { return edid_.refresh(io_); }

    const EdidStore& edid() const noexcept { return edid_; }

    CecStateMachine& cec() noexcept { return cec_; }

private:
    [[nodiscard]] Ex<void> settle_and_publish(adv7513::Adv7513Bus& bus);

    adv7513::Io io_{};
    EdidStore edid_{};
    CecStateMachine cec_{};

    Generation hpd_gen_{};
    Generation audio_gen_{};
    bool hdmi_capable_ = false;
    bool power_requested_ = true;
    bool init_owed_ = false;
    bool sink_powered_ = false;
};

}
