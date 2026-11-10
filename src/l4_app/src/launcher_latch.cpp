// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/launcher_latch.h"

#include <sys/stat.h>

namespace mister::app {

void LauncherLatch::latch(
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

UiRequest::LoadCore LauncherLatch::alias(const UiRequest::LoadCore& req) const noexcept {
    TASTY_SEAT_BODY(LauncherLatch);
    if (image_.empty() || !names_front_end_image(req.path.view(), req.xml))
        return req;
    UiRequest::LoadCore out = req;
    (void)out.path.assign(image_.view());
    return out;
}

void LauncherLatch::publish(
    std::string_view core_name,
    bool direct,
    std::string_view pending_path
) noexcept {
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

void LauncherLatch::withdraw() noexcept {
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

std::optional<UiRequest::LoadCore> LauncherLatch::swap_to_image(SwapGate gate) {
    TASTY_SEAT_BODY(LauncherLatch);
    if (!swap_owed_ || gate.held)
        return std::nullopt;
    if (!gate.room)
        return std::nullopt;
    swap_owed_ = false;
    UiRequest::LoadCore req{.xml = XmlKind::Rbf};
    if (!req.path.assign(image_.view()))
        return std::nullopt;
    return req;
}

}
