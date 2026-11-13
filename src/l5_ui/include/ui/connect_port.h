// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "app/connect_channel.h"
#include "infra/seat.h"
#include "svc/connect_request.h"
#include "ui/connect_demand.h"
#include "ui/connect_refusals.h"
#include "ui/connect_views.h"

namespace mister::ui {

class ConnectPort {
    TASTY_SEAT_RESIDENT(Ui);

public:
    ConnectPort(app::ConnectChannel::Sender sender, app::ConnectChannel::Viewer viewer) noexcept
        : sender_(std::move(sender)),
          viewer_(std::move(viewer)) {}

    ConnectPort(const ConnectPort&) = delete;
    ConnectPort& operator=(const ConnectPort&) = delete;

    [[nodiscard]] bool send(const svc::ConnectRequest& request) noexcept;

    bool pump(ConnectDemand wanted) noexcept;

    [[nodiscard]] bool pending(ConnectDemand wanted) const noexcept {
        return viewer_.pending() || (sender_ && !(accepted_ == wanted));
    }

    [[nodiscard]] const ConnectViews& views() const noexcept { return views_; }

    [[nodiscard]] ConnectDemand accepted() const noexcept { return accepted_; }

    [[nodiscard]] const ConnectRefusals& refusals() const noexcept { return refusals_; }

private:
    app::ConnectChannel::Sender sender_;
    app::ConnectChannel::Viewer viewer_;
    ConnectViews views_{};
    ConnectDemand accepted_{};
    ConnectRefusals refusals_{};
};

}
