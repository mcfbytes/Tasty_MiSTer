// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "infra/park_timeout.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "svc/nic_control.h"
#include "svc/nic_personality.h"
#include "svc/nic_port.h"
#include "svc/nic_status.h"
#include "svc/nic_window_row.h"
#include "svc/nic_window_source.h"

namespace mister::svc {

class NicBridge {
    TASTY_SEAT_RESIDENT(Nic);

public:
    struct Wiring {
        NicControlInbox& inbox;
        INicWindowSource& window;
        INicPort& port;
        NicStatusCell& status;
        NicWindowRow row = kNicWindow;
    };

    explicit NicBridge(const Wiring& w) noexcept;
    NicBridge(const NicBridge&) = delete;
    NicBridge& operator=(const NicBridge&) = delete;
    ~NicBridge();

    void serve() noexcept;

    [[nodiscard]] bool idle() const noexcept { return inbox_.empty(); }

    [[nodiscard]] bool up() const noexcept { return card_ != nullptr; }

    [[nodiscard]] infra::ParkTimeout park_ms() const noexcept {
        return up() ? row_.poll : infra::kForever;
    }

    void stop_card() noexcept;

    void on(const NicControl::Start& s) noexcept;
    void on(const NicControl::Stop& s) noexcept;
    void misrouted(const NicControl& m) noexcept;

    [[nodiscard]] const NicStatus& status() const noexcept { return status_; }

private:
    void publish_() noexcept;

    NicControlInbox& inbox_;
    INicWindowSource& window_;
    INicPort& port_;
    NicStatusCell& cell_;
    NicWindowRow row_;
    std::unique_ptr<INicPersonality> card_;
    NicStatus status_{};
};

}
