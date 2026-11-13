// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "infra/seat.h"
#include "os/ssid.h"
#include "os/wifi_security.h"
#include "ui/connect_slot_plan.h"

namespace mister::ui {

class WifiNetworksPlan final : public ConnectSlotPlan {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit WifiNetworksPlan(const HdPlanInputs& in);

    PageId id() const override;
    std::string_view title() const override;
    bool remembers_focus() const noexcept override;
    ConnectDemand connect_demand() const override;

    bool slot_shown(SlotId slot) const override;
    app::PageDescription::Row slot_describe(SlotId slot) const override;
    Transition slot_select(SlotId slot) override;
    Transition slot_entered(SlotId slot, const TextEntryResult& result) override;

private:
    os::Ssid asked_ssid_{};
    os::WifiSecurity asked_security_ = os::WifiSecurity::Psk;
};

}
