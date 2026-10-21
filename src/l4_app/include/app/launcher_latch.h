// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/stat.h>

#include <optional>
#include <string_view>

#include "app/launcher_demand.h"
#include "app/launcher_profile.h"
#include "app/path_text.h"
#include "app/pending_load.h"
#include "app/ui_request.h"
#include "cores/registry.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "svc/search_policy.h"
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

    void latch(
        const LauncherProfile& l,
        std::string_view core_name,
        std::string_view pending_path
    ) {
        TASTY_SEAT_BODY(LauncherLatch);
        demand_.profile = infra::OptRef<const LauncherProfile>{l};
        const svc::SearchPolicy root{svc::search::kRootOnly, true};
        if (auto p = vfs_.resolve(l.program, root); p) {
            struct ::stat st = {};
            if (::stat(p->c_str(), &st) == 0 && S_ISREG(st.st_mode))
                (void)demand_.program.assign(*p);
        }
        UiRequest::LoadCore request{.xml = XmlKind::Rbf};
        if (request.path.assign(l.front_end_image) && validate_load(vfs_, request))
            (void)image_.assign(l.front_end_image);

        swap_owed_ = !image_.empty()
            && cores::profile_for(core_name).is_front_end
            && !same_image_name(pending_path, image_.view());
    }

    [[nodiscard]] UiRequest::LoadCore alias(const UiRequest::LoadCore& req) const noexcept {
        TASTY_SEAT_BODY(LauncherLatch);
        if (image_.empty() || !names_front_end_image(req.path.view(), req.xml))
            return req;
        UiRequest::LoadCore out = req;
        (void)out.path.assign(image_.view());
        return out;
    }

    void publish(std::string_view core_name, bool direct, std::string_view pending_path) noexcept {
        TASTY_SEAT_BODY(LauncherLatch);
        if (!cell_ || !demand_.profile)
            return;

        demand_.front_end = cores::profile_for(core_name).is_front_end && !swap_owed_;
        demand_.direct_video = direct;
        demand_.scanout_core = demand_.front_end
            && !image_.empty()
            && same_image_name(pending_path, image_.view());
        ++demand_.core_gen;
        cell_->publish(demand_);
        if (wake_)
            wake_->kick_if_armed();
    }

    void withdraw() noexcept {
        TASTY_SEAT_BODY(LauncherLatch);
        if (!cell_ || !demand_.profile)
            return;
        if (!demand_.front_end)
            return;
        demand_.front_end = false;
        demand_.scanout_core = false;
        ++demand_.core_gen;
        cell_->publish(demand_);
        if (wake_)
            wake_->kick_if_armed();
    }

    [[nodiscard]] std::optional<UiRequest::LoadCore> swap_to_image(bool held, bool room) {
        TASTY_SEAT_BODY(LauncherLatch);
        if (!swap_owed_ || held)
            return std::nullopt;
        if (!room)
            return std::nullopt;
        swap_owed_ = false;
        UiRequest::LoadCore req{.xml = XmlKind::Rbf};
        if (!req.path.assign(image_.view()))
            return std::nullopt;
        return req;
    }

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
