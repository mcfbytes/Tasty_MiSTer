// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/load_belt.h"

#include <algorithm>
#include <type_traits>
#include <utility>

#include "app/event.h"

namespace mister::app {

bool LoadBelt::offer(BeltOrders&& orders) {
    TASTY_SEAT_BODY(LoadBelt);
    if (orders.addons_after)
        addons_after_.emplace(std::move(*orders.addons_after));
    if (orders.addons)
        addons_.emplace(std::move(*orders.addons));
    const bool steps = orders.pieces || orders.load || orders.walk;
    if (steps && any_live(kLoaderBusy)) {
        ++tally_.belt_offers_refused;
        ++tally_.files_failed;
        return false;
    }
    if (orders.pieces)
        pieces_.emplace(std::move(*orders.pieces));
    if (orders.load)
        load_.emplace(std::move(*orders.load));
    if (orders.walk)
        walk_.emplace(std::move(*orders.walk));
    return true;
}

void LoadBelt::offer(ManifestLadder&& m) {
    TASTY_SEAT_BODY(LoadBelt);
    manifest_.emplace(std::move(m));
}

void LoadBelt::offer(CompanionWalk&& c) {
    TASTY_SEAT_BODY(LoadBelt);
    companion_walk_.emplace(std::move(c));
}

void LoadBelt::offer(LoadWalk&& w, std::uint32_t gen) {
    TASTY_SEAT_BODY(LoadBelt);
    walk_.emplace(std::move(w));
    walk_gen_ = gen;
    walk_status_.state = cores::MountState::Pending;
}

void LoadBelt::hold(const SaveStateHold& h) {
    TASTY_SEAT_BODY(LoadBelt);
    ss_hold_ = h;
}

void LoadBelt::refuse_walk(std::uint32_t gen) noexcept {
    TASTY_SEAT_BODY(LoadBelt);
    walk_status_ = cores::MountStatus{.generation = gen, .state = cores::MountState::Failed};
}

void LoadBelt::step(Host& host, cores::ILadderHost& ladder) {
    TASTY_SEAT_BODY(LoadBelt);
    for (const RungRow& row : kRungRows) {
        switch (row.source) {
            case RungSource::Held:
                host.step_held(row.slot);
                break;
            case RungSource::Role: {

                const infra::OptRef<Rung> live = rung_(row.slot);
                if (!live)
                    break;
                settle_(row.slot, live->step(edges_, ladder), host);
                break;
            }
        }
    }
}

void LoadBelt::step_companion(Host& host, cores::ILadderHost& ladder) {
    TASTY_SEAT_BODY(LoadBelt);
    if (!companion_walk_)
        return;
    settle_(RungSlot::Companion, companion_walk_->step(edges_, ladder), host);
}

LoadBelt::Wait LoadBelt::wait(const Host& host) const noexcept {
    TASTY_SEAT_BODY(LoadBelt);
    Wait soonest = Wait::None;
    for (const RungRow& row : kRungRows) {
        Wait w = Wait::None;
        switch (row.source) {
            case RungSource::Held:
                w = host.held_wait(row.slot);
                break;
            case RungSource::Role:
                if (const infra::OptRef<const Rung> live = rung_(row.slot)) {
                    w = live->wants_pass(edges_.inbox, edges_.level) ? Wait::Now : Wait::Later;
                }
                break;
        }
        soonest = std::max(soonest, w);
    }
    return soonest;
}

infra::OptRef<const LoadBelt::SaveStateHold> LoadBelt::savestate_hold() const noexcept {
    TASTY_SEAT_BODY(LoadBelt);
    return ss_hold_ ? infra::OptRef<const SaveStateHold>{*ss_hold_}
                    : infra::OptRef<const SaveStateHold>{};
}

std::optional<LoadBelt::SaveStateHold> LoadBelt::take_savestate_hold() noexcept {
    TASTY_SEAT_BODY(LoadBelt);
    return std::exchange(ss_hold_, std::nullopt);
}

bool LoadBelt::addons_after_owed() const noexcept {
    TASTY_SEAT_BODY(LoadBelt);
    return !addons_ && addons_after_.has_value();
}

void LoadBelt::promote_addons_after() noexcept {
    TASTY_SEAT_BODY(LoadBelt);
    if (!addons_after_owed())
        return;
    addons_ = std::move(addons_after_);
    addons_after_.reset();
}

std::optional<Answered<cores::MountStatus>> LoadBelt::take_walk_answer() noexcept {
    TASTY_SEAT_BODY(LoadBelt);
    return std::exchange(walk_answered_, std::nullopt);
}

void LoadBelt::cancel() noexcept {
    TASTY_SEAT_BODY(LoadBelt);
    pieces_.reset();
    addons_.reset();
    ss_hold_.reset();
    addons_after_.reset();

    manifest_.reset();
    if (load_) {
        load_.reset();
        edges_.windows.cancelled();
        edges_.windows.publish();
        auto events = edges_.events;
        events.push(Event::ProgressUpdate{}, Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused});
    }
    walk_gen_ = 0;
    walk_answered_.reset();
    walk_status_ = cores::MountStatus{};
    if (walk_) {
        walk_.reset();
        edges_.windows.cancelled();
        edges_.windows.publish();
    }
}

void LoadBelt::drop_manifest() noexcept {
    TASTY_SEAT_BODY(LoadBelt);
    manifest_.reset();
}

void LoadBelt::drop_companion() noexcept {
    TASTY_SEAT_BODY(LoadBelt);
    companion_walk_.reset();
}

RungMask LoadBelt::live() const noexcept {
    TASTY_SEAT_BODY(LoadBelt);
    RungMask m{};
    if (addons_)
        m.set(RungSlot::Addons);
    if (addons_after_)
        m.set(RungSlot::AddonsAfter);
    if (ss_hold_)
        m.set(RungSlot::SavestateHold);
    if (pieces_)
        m.set(RungSlot::Pieces);
    if (load_)
        m.set(RungSlot::Load);
    if (manifest_)
        m.set(RungSlot::Manifest);
    if (walk_)
        m.set(RungSlot::Walk);
    if (companion_walk_)
        m.set(RungSlot::Companion);
    return m;
}

template <class Self>
auto LoadBelt::rung_in_(Self& self, RungSlot slot) noexcept {
    using R = std::conditional_t<std::is_const_v<Self>, const Rung, Rung>;
    const auto live = [](auto& o) noexcept {
        return o ? infra::OptRef<R>{*o} : infra::OptRef<R>{};
    };
    switch (slot) {
        case RungSlot::Addons:
            return live(self.addons_);
        case RungSlot::Pieces:
            return live(self.pieces_);
        case RungSlot::Load:
            return live(self.load_);
        case RungSlot::Manifest:
            return live(self.manifest_);
        case RungSlot::Walk:
            return live(self.walk_);
        case RungSlot::Companion:
            return live(self.companion_walk_);
        case RungSlot::BootLadder:
        case RungSlot::SavestateHold:
        case RungSlot::SaveMount:
        case RungSlot::AddonsAfter:
        case RungSlot::Watch:
        case RungSlot::kCount:
            break;
    }
    return infra::OptRef<R>{};
}

infra::OptRef<Rung> LoadBelt::rung_(RungSlot slot) noexcept {
    return rung_in_(*this, slot);
}

infra::OptRef<const Rung> LoadBelt::rung_(RungSlot slot) const noexcept {
    return rung_in_(*this, slot);
}

void LoadBelt::settle_(RungSlot slot, Rung::Pass pass, Host& host) {
    switch (slot) {
        case RungSlot::Addons:
            return close_addons_(pass, host);
        case RungSlot::Pieces:
            return close_pieces_(pass, host);
        case RungSlot::Load:
            return close_load_(pass, host);
        case RungSlot::Manifest:
            return close_manifest_(pass, host);
        case RungSlot::Walk:
            return close_walk_(pass, host);
        case RungSlot::Companion:
            if (pass == Rung::Pass::Done)
                companion_walk_.reset();
            return;
        case RungSlot::BootLadder:
        case RungSlot::SavestateHold:
        case RungSlot::SaveMount:
        case RungSlot::AddonsAfter:
        case RungSlot::Watch:
        case RungSlot::kCount:
            return;
    }
}

void LoadBelt::close_addons_(Rung::Pass pass, Host& host) {
    if (pass == Rung::Pass::Waiting)
        return;
    tally_.addons_sent += addons_->sent();
    tally_.addons_missing += addons_->missing();
    if (addons_->image_sent())
        ++tally_.ram_images_sent;
    if (addons_->image_failed()) {
        ++tally_.ram_images_declined;
        auto events = edges_.events;
        events.push(
            Event::RamImageDeclined{.why = Errc::io},
            Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}
        );
    }
    addons_.reset();
    host.addons_closed();
}

void LoadBelt::close_pieces_(Rung::Pass pass, Host& host) {
    if (pass == Rung::Pass::Done) {
        if (!pieces_->resumed()) {
            ++tally_.files_loaded;
            host.content_loaded();
        }
        host.last_piece_sent(*pieces_);
    } else if (pass == Rung::Pass::Failed) {
        ++tally_.files_failed;
    } else {
        return;
    }
    pieces_.reset();
}

void LoadBelt::close_load_(Rung::Pass pass, Host& host) {
    if (pass != Rung::Pass::Done && pass != Rung::Pass::Failed)
        return;
    load_.reset();
    if (pass == Rung::Pass::Done) {
        ++tally_.files_loaded;
        edges_.windows.opened();
        host.content_loaded();
    } else {
        ++tally_.files_failed;
    }
    edges_.windows.publish();
}

void LoadBelt::close_manifest_(Rung::Pass pass, Host& host) {
    edges_.windows.publish();
    if (pass == Rung::Pass::Failed) {
        const Errc why = manifest_->failure();
        manifest_.reset();
        host.manifest_failed(why);
        return;
    }
    if (pass != Rung::Pass::Done)
        return;

    if (!manifest_->error().empty()) {
        ++tally_.manifest_errors;
        auto events = edges_.events;
        events.push(
            Event::InfoRequest{.id = InfoId::RomLoadFailed},
            Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}
        );
    }
    manifest_.reset();
    host.manifest_done();
}

void LoadBelt::close_walk_(Rung::Pass pass, Host& host) {
    if (pass == Rung::Pass::Done) {
        ++tally_.files_loaded;
        host.content_loaded();
    } else if (pass == Rung::Pass::Failed) {
        ++tally_.files_failed;
        if (walk_->refused() || walk_->stalled()) {
            auto events = edges_.events;
            events.push(
                Event::InfoRequest{.id = InfoId::RomLoadFailed},
                Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}
            );
        }
    } else {
        return;
    }
    if (walk_gen_ != 0) {
        walk_status_ = cores::MountStatus{
            .generation = walk_gen_,
            .state = pass == Rung::Pass::Done ? cores::MountState::Done : cores::MountState::Failed
        };
        walk_answered_ = Answered<cores::MountStatus>{.gen = walk_gen_, .value = walk_status_};
        walk_gen_ = 0;
    }
    walk_.reset();
}

}
