// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/servant_mounts.h"

#include <string>
#include <string_view>

#include "app/start_chain.h"
#include "infra/prelude.h"
#include "svc/confine_under.h"

namespace mister::app {

namespace {

[[nodiscard]] MountQueue::MountAsk servant_ask(
    proto::SlotIndex slot,
    std::string_view path,
    MountQueue::MountAsk::Servant kind
) noexcept {
    MountQueue::MountAsk ask{
        .which = UiRequest::Kind::MountImage,
        .index = slot.io_index(),
        .tag = proto::kUncaused,
        .servant = kind
    };

    if (!ask.path.assign(path))
        ask.path = {};
    return ask;
}

}

void ServantMounts::begin_session() noexcept {
    TASTY_SEAT_BODY(ServantMounts);
    control_due_ = os::Deadline::immediate();
    remount_owed_.reset();
    repulse_due_.reset();
    own_mount_.reset();
    (void)remount_reader_.take_if_changed(remount_);
    (void)reads_reader_.take_if_changed(reads_);

    seen_ = {};
}

std::string ServantMounts::directory_of(const cores::ControlSlot& row, const Facts& facts) {
    if (row.root == cores::ControlSlot::Root::SharedFolder && !facts.shared_folder.empty())
        return std::string(facts.shared_folder);
    std::string dir = "games/";
    dir.append(facts.core.view());
    dir.push_back('/');
    dir.append(row.subdir);
    return dir;
}

void ServantMounts::step(const Facts& facts, MountQueue& mounts, MountQueue::Host& host) {
    TASTY_SEAT_BODY(ServantMounts);
    if (facts.core.empty())
        return;

    if (const auto asked = remount_reader_.take_if_changed(remount_))
        remount_owed_ = *asked;
    if (facts.profile.grants.has(cores::Grant::Servants)) {
        if (facts.profile.repulse.declared())
            step_repulse_(facts.profile.repulse, mounts, host);
        if (!mounts.idle())
            return;
        if (step_remount_(facts, mounts, host))
            return;
        (void)step_control_(facts, mounts, host);
        return;
    }

    if (!mounts.idle())
        return;
    (void)step_remount_(facts, mounts, host);
}

bool ServantMounts::step_remount_(const Facts& facts, MountQueue& mounts, MountQueue::Host& host) {
    if (!remount_owed_)
        return false;
    const cores::RemountAsked owed = *remount_owed_;
    remount_owed_.reset();
    if (owed.path.empty())
        return false;
    if (facts.profile.grants.has(cores::Grant::Servants)) {
        const MountQueue::MountAsk ask = servant_ask(
            owed.slot,
            owed.path.view(),
            MountQueue::MountAsk::Servant::Image
        );
        if (ask.path.empty())
            return false;
        mounts.on_mount_image(host, ask);
        return true;
    }
    const std::string home = StartChain::core_home(facts.core.view(), vfs_);
    const Ex<std::string> path = svc::ConfineRoot{home}.confine(vfs_, owed.path.view());
    MountQueue::MountAsk ask{.index = owed.slot.io_index(), .tag = proto::kUncaused};
    if (!path || !ask.path.assign(*path)) {
        refused_names_.add();
        return false;
    }
    mounts.on_mount_image(host, ask);
    return true;
}

bool ServantMounts::step_control_(const Facts& facts, MountQueue& mounts, MountQueue::Host& host) {
    if (!control_due_.expired(clock_))
        return false;
    control_due_ = os::Deadline::in(clock_, kControlPeriod);
    for (const cores::ControlSlot& row : facts.profile.control_slots) {

        if (mounts.announced(row.slot))
            continue;
        const std::string dir = directory_of(row, facts);
        if (row.announce == cores::ControlSlot::Announce::WhenDirectory && !vfs_.dir_exists(dir))
            continue;
        const MountQueue::MountAsk ask = servant_ask(
            row.slot,
            dir,
            MountQueue::MountAsk::Servant::Control
        );
        if (ask.path.empty())
            continue;
        mounts.on_mount_image(host, ask);

        control_due_ = os::Deadline::immediate();
        return true;
    }
    return false;
}

void ServantMounts::step_repulse_(
    const cores::RepulseRule& rule,
    MountQueue& mounts,
    MountQueue::Host& host
) {
    if (const auto now = reads_reader_.take_if_changed(reads_)) {
        if (now->mount != seen_.mount) {
            const bool own = own_mount_ == now->mount;
            own_mount_.reset();
            repulse_due_.reset();
            if (now->translated && !own)
                repulse_due_ = os::Deadline::in(clock_, rule.after);
        }
        seen_ = *now;
    }

    if (own_mount_ && mounts.idle())
        own_mount_.reset();
    if (!repulse_due_ || !repulse_due_->expired(clock_) || !mounts.idle())
        return;
    repulse_due_.reset();
    if (!seen_.translated || seen_.data_reads > rule.unread_reads)
        return;
    const MountQueue::MountAsk ask = servant_ask(
        rule.slot,
        mounts.mounted_path(rule.slot),
        MountQueue::MountAsk::Servant::Image
    );
    if (ask.path.empty())
        return;
    own_mount_ = seen_.mount.next();
    mounts.on_mount_image(host, ask);
}

}
