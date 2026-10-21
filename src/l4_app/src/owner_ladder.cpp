// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/owner_ladder.h"

#include <utility>

#include "cores/ladder_context.h"

namespace mister::app {

OwnerLadder::OwnerLadder(
    const LinkTxChannel<SeatTag::Main>& inbox,
    LadderStateCell& cell,
    OwnerTally& tally,
    const svc::Vfs& vfs,
    const os::IClock& clock
) noexcept
    : inbox_(inbox),
      cell_(cell),
      tally_(tally),
      vfs_(vfs),
      clock_(clock) {}

bool OwnerLadder::live() const noexcept {
    TASTY_SEAT_BODY(OwnerLadder);
    return ladder_ != nullptr;
}

bool OwnerLadder::moved() const noexcept {
    TASTY_SEAT_BODY(OwnerLadder);
    return moved_;
}

bool OwnerLadder::start_armed() const noexcept {
    TASTY_SEAT_BODY(OwnerLadder);
    return start_;
}

bool OwnerLadder::noreset() const noexcept {
    TASTY_SEAT_BODY(OwnerLadder);
    return noreset_;
}

void OwnerLadder::clear_noreset() noexcept {
    TASTY_SEAT_BODY(OwnerLadder);
    noreset_ = false;
}

void OwnerLadder::restore_noreset(bool carried) noexcept {
    TASTY_SEAT_BODY(OwnerLadder);
    noreset_ = carried;
}

bool OwnerLadder::arm(
    const MountAsk& ask,
    const cores::CoreFactory& row,
    cores::ILadderHost& host
) {
    TASTY_SEAT_BODY(OwnerLadder);
    const cores::LadderContext ctx{
        .vfs = vfs_,
        .host = host,
        .clock = clock_,
        .image_path = ask.path.view(),
        .last_dir = last_dir_,
        .noreset = noreset_,
        .slot = ask.index,
        .slot_paths = slot_paths_
    };
    ladder_ = row.make_ladder != nullptr ? row.make_ladder(*row.profile, ctx) : nullptr;
    if (ladder_ == nullptr)
        return false;

    last_dir_.assign(ladder_->next_last_dir());
    moved_ = true;
    owed_ = Owed{ask.which, ask.tag};
    slot_ = ask.index.v;
    ++tally_.ladders_run;
    publish_(true);
    return true;
}

OwnerLadder::Step OwnerLadder::step() {
    TASTY_SEAT_BODY(OwnerLadder);
    if (ladder_ == nullptr)
        return {};
    const std::uint32_t before = inbox_.ring().pushed();
    const std::uint32_t loaded = ladder_->report().assets_loaded;
    const std::uint32_t missing = ladder_->report().bios_missing;
    auto r = ladder_->step();
    moved_ = inbox_.ring().pushed() != before;
    tally_.files_loaded += ladder_->report().assets_loaded - loaded;
    const bool bios_missing = ladder_->report().bios_missing != missing;
    if (!r) {
        return {finish_(false), bios_missing};
    }
    if (*r) {
        return {finish_(true), bios_missing};
    }
    publish_(true);
    return {std::nullopt, bios_missing};
}

std::optional<OwnerLadder::Ended> OwnerLadder::finish_(bool ok) {
    if (ladder_ == nullptr)
        return std::nullopt;
    if (!ok)
        ++tally_.ladder_failures;
    publish_(false);

    last_dir_.assign(ladder_->next_last_dir());
    noreset_ = ladder_->next_noreset();
    const auto paths = ladder_->next_slot_paths();
    for (std::size_t i = 0; i < paths.size() && i < slot_paths_.size(); ++i)
        slot_paths_[i] = paths[i];
    const cores::BootLadder::Report rep = ladder_->report();
    if (rep.savestate_hold_expired)
        ++tally_.savestate_hold_expiries;

    const bool touched = (!start_ && !core_) || rep.save_mounted || rep.save_detached;
    ladder_.reset();
    moved_ = false;
    start_ = false;
    core_ = false;
    return Ended{.ok = ok, .touched = touched, .slot = slot_, .owed = std::exchange(owed_, Owed{})};
}

void OwnerLadder::publish_(bool live) {
    LadderCell rec{};
    if (ladder_ != nullptr) {
        rec.report = ladder_->report();
        rec.pc = ladder_->pc();
    }
    rec.live = live;
    cell_.publish(rec);
}

void OwnerLadder::drop() {
    TASTY_SEAT_BODY(OwnerLadder);
    if (ladder_ != nullptr) {
        publish_(false);
        ladder_.reset();
    }
    moved_ = false;
    start_ = false;
    core_ = false;
    last_dir_.clear();
    noreset_ = false;
    for (std::string& p : slot_paths_)
        p.clear();
}

bool OwnerLadder::arm_start(
    const FixedStr<64, StrFit::Clip>& core_name,
    bool index0_taken,
    cores::ILadderHost& host
) {
    TASTY_SEAT_BODY(OwnerLadder);
    if (core_name.empty() || ladder_ != nullptr)
        return false;
    const auto row = cores::find_core(core_name.view());
    if (!row || (*row)->make_ladder == nullptr)
        return false;
    const cores::StagingPolicy& sp = (*row)->profile->staging;
    if (sp.start_save_dir.empty() && (*row)->profile->start_assets.empty())
        return false;
    const cores::LadderContext ctx{
        .vfs = vfs_,
        .host = host,
        .clock = clock_,
        .image_path = {},
        .last_dir = {},
        .noreset = false,
        .at_core_start = true,
        .index0_taken = index0_taken
    };
    ladder_ = (*row)->make_ladder(*(*row)->profile, ctx);
    if (ladder_ == nullptr)
        return false;
    start_ = true;
    owed_ = Owed{};
    slot_ = sp.save_slot.v;
    last_dir_.assign(ladder_->next_last_dir());
    moved_ = true;
    ++tally_.ladders_run;
    publish_(true);
    return true;
}

bool OwnerLadder::arm_core(
    std::optional<proto::ResetEdge> reset,
    std::string_view config,
    const cores::CoreFactory& row,
    cores::ILadderHost& host,
    Owed owed
) {
    TASTY_SEAT_BODY(OwnerLadder);
    if (ladder_ != nullptr || row.make_ladder == nullptr)
        return false;
    const cores::LadderContext ctx{
        .vfs = vfs_,
        .host = host,
        .clock = clock_,
        .image_path = config,
        .last_dir = last_dir_,
        .noreset = noreset_,
        .reset = reset,
        .restore = !reset.has_value(),
        .slot_paths = slot_paths_
    };
    ladder_ = row.make_ladder(*row.profile, ctx);
    if (ladder_ == nullptr)
        return false;
    core_ = true;
    owed_ = owed;
    slot_ = 0;
    moved_ = true;
    ++tally_.ladders_run;
    publish_(true);
    return true;
}

}
