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
    return kind_ == LadderKind::Start;
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
        .last_dir = last_dir_,
        .noreset = noreset_,
        .slot_paths = slot_paths_,
        .arm = cores::LadderContext::MountArm{ask.index, ask.path.view()}
    };
    ladder_ = row.make_ladder != nullptr ? row.make_ladder(row.profile, ctx) : nullptr;
    if (ladder_ == nullptr)
        return false;

    last_dir_.assign(ladder_->carry().last_dir);
    kind_ = LadderKind::Mount;
    moved_ = true;
    owed_ = Owed{ask.which, ask.tag};
    index_ = ask.index;
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

    const cores::LadderCarry carry = ladder_->carry();
    last_dir_.assign(carry.last_dir);
    noreset_ = carry.noreset;
    for (std::size_t i = 0; i < carry.slot_paths.size() && i < slot_paths_.size(); ++i)
        slot_paths_[i] = carry.slot_paths[i];
    const cores::BootLadder::Report rep = ladder_->report();
    if (rep.savestate_hold_expired)
        ++tally_.savestate_hold_expiries;

    const bool touched = kind_ == LadderKind::Mount || rep.save_mounted || rep.save_detached;
    ladder_.reset();
    moved_ = false;
    kind_ = LadderKind::None;
    return Ended{
        .ok = ok,
        .touched = touched,
        .index = index_,
        .owed = std::exchange(owed_, Owed{})
    };
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
    kind_ = LadderKind::None;
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
    if (!row || row->make_ladder == nullptr)
        return false;
    const cores::StagingPolicy& sp = row->profile.staging;
    const bool start_save = sp.save && !sp.save->start_dir.empty();
    if (!start_save && row->profile.start_assets.empty())
        return false;
    const cores::LadderContext ctx{
        .vfs = vfs_,
        .host = host,
        .clock = clock_,
        .last_dir = {},
        .noreset = false,
        .arm = cores::LadderContext::CoreStartArm{index0_taken}
    };
    ladder_ = row->make_ladder(row->profile, ctx);
    if (ladder_ == nullptr)
        return false;
    kind_ = LadderKind::Start;
    owed_ = Owed{};
    index_ = sp.save ? sp.save->slot : proto::IoIndex{};
    last_dir_.assign(ladder_->carry().last_dir);
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
    cores::LadderContext ctx{
        .vfs = vfs_,
        .host = host,
        .clock = clock_,
        .last_dir = last_dir_,
        .noreset = noreset_,
        .slot_paths = slot_paths_
    };
    if (reset) {
        ctx.arm = cores::LadderContext::ResetArm{*reset};
    } else {
        ctx.arm = cores::LadderContext::RestoreArm{config};
    }
    ladder_ = row.make_ladder(row.profile, ctx);
    if (ladder_ == nullptr)
        return false;
    kind_ = LadderKind::Core;
    owed_ = owed;
    index_ = {};
    moved_ = true;
    ++tally_.ladders_run;
    publish_(true);
    return true;
}

}
