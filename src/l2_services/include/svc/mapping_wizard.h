// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/device_identity.h"
#include "svc/map_store.h"

namespace mister::svc {

class MappingWizard {
    TASTY_SEAT_EXEMPT(component);

public:
    enum class Step : std::uint8_t {
        Idle,
        AwaitDevice,
        AwaitButton,
        Confirm,
        Persist,
        Done,
        Cancelled,
    };

    Step step() const noexcept { return step_; }
    Ex<void> begin(const DeviceIdentity& id, MapKind kind);
    Ex<Step> advance();
    void cancel();

    void on_device(const DeviceIdentity& id);
    void on_button(std::uint16_t code);

    const DeviceIdentity& device() const noexcept { return device_; }
    MapKind kind() const noexcept { return kind_; }
    std::optional<std::uint16_t> captured() const noexcept {
        return has_capture_ ? std::optional<std::uint16_t>(captured_) : std::nullopt;
    }

private:
    Step step_ = Step::Idle;
    DeviceIdentity device_{};
    MapKind kind_ = MapKind::Joystick;
    std::uint16_t captured_ = 0;
    bool has_capture_ = false;
};

}  // namespace mister::svc
