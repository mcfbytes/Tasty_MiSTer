// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/adv7513_io.h"
#include "svc/cec_state_machine.h"
#include "svc/edid_store.h"
#include "svc/types.h"

namespace mister::svc {

class HdmiLink {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit HdmiLink(adv7513::Io io) noexcept;
    HdmiLink(const HdmiLink&) = delete;
    HdmiLink& operator=(const HdmiLink&) = delete;

    void attach() noexcept {
        attached_ = static_cast<bool>(io_.main) && static_cast<bool>(io_.hdmi);
    }

    void set_hdmi_capable(bool v) noexcept { hdmi_capable_ = v; }

    bool hdmi_capable() const noexcept { return hdmi_capable_; }

    [[nodiscard]] Ex<void> poll_hotplug();

    [[nodiscard]] Ex<void> request_power(bool on);

    bool power_requested() const noexcept { return power_requested_; }

    bool sink_powered() const noexcept { return sink_powered_; }

    bool init_owed() const noexcept { return init_owed_; }

    void clear_init_owed() noexcept { init_owed_ = false; }

    Generation audio_generation() const noexcept { return audio_gen_; }

    Generation hotplug_generation() const noexcept { return hpd_gen_; }

    EdidStore& edid() noexcept { return edid_; }

    CecStateMachine& cec() noexcept { return cec_; }

private:
    [[nodiscard]] Ex<void> apply_tmds_power(bool on);

    [[nodiscard]] Ex<void> settle_and_publish();

    adv7513::Io io_{};
    EdidStore edid_{io_};
    CecStateMachine cec_{io_};

    Generation hpd_gen_{};
    Generation audio_gen_{};
    bool hdmi_capable_ = false;
    bool power_requested_ = true;
    bool init_owed_ = false;
    bool sink_powered_ = false;

    bool attached_ = false;
};

}
