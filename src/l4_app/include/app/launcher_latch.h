// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "app/launcher_demand.h"
#include "app/launcher_profile.h"
#include "app/path_text.h"
#include "app/bitstream_resolve.h"
#include "app/ui_request.h"
#include "cores/registry.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "svc/vfs.h"

namespace mister::app {

class LauncherLatch final {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit LauncherLatch(const svc::Vfs& vfs) noexcept : vfs_(vfs) {}

    void bind(
        infra::OptRef<LauncherDemandCell> demand_cell,
        infra::OptRef<xthread::WakeFlag> host_wake
    ) noexcept {
        cell_ = demand_cell;
        wake_ = host_wake;
    }

    [[nodiscard]] infra::OptRef<const LauncherProfile> profile() const noexcept {
        return demand_.profile;
    }

    void latch(const LauncherProfile& l, std::string_view core_name, std::string_view pending_path);

    [[nodiscard]] UiRequest::LoadCore alias(const UiRequest::LoadCore& req) const noexcept;

    void publish(std::string_view core_name, bool direct, std::string_view pending_path) noexcept;

    void withdraw() noexcept;

    struct SwapGate {
        bool held = false;
        bool room = false;
    };

    [[nodiscard]] std::optional<UiRequest::LoadCore> swap_to_image(SwapGate gate);

    [[nodiscard]] const LauncherDemand& demand() const noexcept { return demand_; }

    [[nodiscard]] bool swap_owed() const noexcept { return swap_owed_; }

    [[nodiscard]] std::string_view image() const noexcept { return image_.view(); }

private:
    const svc::Vfs& vfs_;
    LauncherDemand demand_{};
    PathText image_{};
    infra::OptRef<LauncherDemandCell> cell_{};
    infra::OptRef<xthread::WakeFlag> wake_{};
    bool swap_owed_ = false;
};

}
