// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/save_stage_director.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <utility>

namespace mister::app {

using proto::SaveStageRow;
using proto::SaveStageSlot;
using proto::StageState;

namespace {

template <std::size_t... I>
consteval std::array<SaveStageSlot, sizeof...(I)> slots_of(std::index_sequence<I...>) {
    return {SaveStageSlot{I}...};
}

constexpr auto kSlots = slots_of(std::make_index_sequence<proto::kMaxStagedSaves.size()>{});

constexpr infra::ParkTimeout kSettleWake{proto::kSaveSettlePeriod};
constexpr infra::ParkTimeout kUserPassWake{proto::kUserPassPoll};

[[nodiscard]] bool unsaved(const SaveStageRow& r) noexcept {
    return r.rev != r.saved;
}

[[nodiscard]] bool resident(const SaveStageRow& r) noexcept {
    return r.state == StageState::Live || r.state == StageState::Retiring;
}

[[nodiscard]] bool quiet(
    const SaveStageRow& r,
    SaveStageDirector::TimePoint now,
    std::chrono::milliseconds bound = proto::kSavePassQuiet
) noexcept {
    return now - r.last_write >= bound;
}

}

bool SaveStageDirector::read_() noexcept {
    proto::SaveStageReport r{};
    if (report_cell_.sample_into(r) == 0) {
        tally_.report_refusals.add();
        return false;
    }
    for (const SaveStageSlot s : kSlots)
        stages_[s].row = r.rows[s];
    summary_ = r.summary;
    return true;
}

std::optional<proto::ResetEdge> SaveStageDirector::step(const Facts& facts) noexcept {
    TASTY_SEAT_BODY(SaveStageDirector);
    facts_ = facts;
    if (!read_())
        return std::nullopt;
    const TimePoint now = clock_.now();
    rows_();
    results_(now);
    if (window_opened_)
        close_window_(now);
    if (!window_opened_)
        passes_outside_();
    stamp_unsaved_(now);
    order_unsaved_(now, facts_);
    publish_(facts_);
    return probe_due_(now, facts_);
}

std::optional<proto::ResetEdge> SaveStageDirector::probe_due_(
    TimePoint now,
    const Facts& facts
) noexcept {
    enabled_ = facts.enabled;
    if (!facts.probe)
        return std::nullopt;
    const Facts::ProbeFacts& p = *facts.probe;
    if (!(p.conf_gen == seen_gen_)) {
        seen_gen_ = p.conf_gen;
        due_.reset();
        if (!(p.conf_gen == proto::BindGeneration{}))
            due_ = now + proto::kAutosaveProbePeriod;
    }
    if (!due_ || now < *due_)
        return std::nullopt;
    if (!facts.enabled || !status_.probing || !p.usable)
        return std::nullopt;
    if (p.menu_focus && !status_.holding)
        return std::nullopt;
    due_ = now + proto::kAutosaveProbePeriod;
    return p.edge;
}

void SaveStageDirector::rows_() noexcept {
    for (Stage& st : stages_) {
        if (st.watch.key == st.row.key)
            continue;
        st.watch = StageWatch{.key = st.row.key, .commits_base = st.row.commits};
    }
}

void SaveStageDirector::results_(TimePoint now) noexcept {
    for (Stage& st : stages_) {
        const SaveStageRow& row = st.row;
        StageWatch& w = st.watch;
        if (!w.ordered)
            continue;
        const Ordered o = *w.ordered;
        if (row.saved >= o.rev) {
            if (row.commits == o.commits) {
                tally_.identical.add();
            } else {
                count_(o.cause);
            }
            if (unsaved(row)) {
                w.unsaved_since = now;
            } else {
                w.unsaved_since.reset();
                w.held.reset();
                w.was_held = false;
            }
            w.ordered.reset();
            w.in_episode = false;
        } else if (row.faults != o.faults) {
            status_.last_fault = row.last_fault;
            w.retry_at = now + proto::kSaveCommitRetry;
            if (!w.in_episode)
                status_.fault_episodes.add();
            w.in_episode = true;
            w.ordered.reset();
        } else if (row.declines != o.declines || !resident(row)) {
            w.ordered.reset();
        }
    }
}

void SaveStageDirector::close_window_(TimePoint now) noexcept {
    const auto open_for = now - *window_opened_;
    if (open_for < proto::kProbeAnswerWindow)
        return;
    if (open_for < proto::kProbeWindowMax) {
        for (const Stage& st : stages_) {
            if (st.row.state == StageState::Live && !quiet(st.row, now))
                return;
        }
    } else {
        tally_.windows_bounded.add();
    }
    for (Stage& st : stages_) {
        const SaveStageRow& row = st.row;
        StageWatch& w = st.watch;
        const std::optional<proto::StageRevision> at_open = std::exchange(w.at_open, std::nullopt);
        if (row.state != StageState::Live || !at_open)
            continue;
        if (row.rev != *at_open) {
            if (!unsaved(row))
                continue;
            w.held = row.rev;
            w.was_held = true;
        } else if (w.held == row.rev) {
            w.held.reset();
        }
    }
    window_opened_.reset();
    last_close_ = now;
}

void SaveStageDirector::passes_outside_() noexcept {
    if (!last_close_)
        return;
    for (Stage& st : stages_) {
        if (st.watch.held && st.row.last_write > *last_close_)
            st.watch.held.reset();
    }
}

void SaveStageDirector::stamp_unsaved_(TimePoint now) noexcept {
    for (Stage& st : stages_) {
        if (unsaved(st.row) && !st.watch.unsaved_since)
            st.watch.unsaved_since = now;
    }
}

void SaveStageDirector::order_unsaved_(TimePoint now, const Facts& facts) noexcept {
    for (Stage& st : stages_) {
        const SaveStageRow& row = st.row;
        StageWatch& w = st.watch;
        if (!unsaved(row) || w.ordered || (w.retry_at && now < *w.retry_at))
            continue;
        if (row.state == StageState::Retiring) {
            order_(w, row, Cause::Forced, facts);
            continue;
        }

        const bool user = !w.held && !w.was_held;
        const auto bound = user ? proto::kUserPassQuiet : proto::kSavePassQuiet;
        if (row.state != StageState::Live || window_opened_ || !quiet(row, now, bound))
            continue;
        const bool overdue = w.unsaved_since
            && now - *w.unsaved_since >= proto::kAutosaveMaxDeferral;
        if (w.held && !overdue)
            continue;
        const Cause cause = w.held ? Cause::Deferred
            : w.was_held           ? Cause::Confirmed
                                   : Cause::Direct;
        order_(w, row, cause, facts);
    }
}

void SaveStageDirector::order_(
    StageWatch& w,
    const SaveStageRow& row,
    Cause cause,
    const Facts& facts
) noexcept {
    const proto::SaveStageOrder::Commit c{
        .key = row.key,
        .rev = row.rev,
        .backup = backup_for_(w, row, facts)
    };
    if (!orders_.push(infra::make<proto::SaveStageOrder>(c))) {
        tally_.orders_refused.add();
        return;
    }
    w.ordered = Ordered{
        .rev = row.rev,
        .cause = cause,
        .commits = row.commits,
        .faults = row.faults,
        .declines = row.declines
    };
}

void SaveStageDirector::count_(Cause cause) noexcept {
    switch (cause) {
        case Cause::Confirmed:
            status_.confirmed.add();
            return;
        case Cause::Direct:
            status_.direct.add();
            return;
        case Cause::Deferred:
            status_.deferred.add();
            return;
        case Cause::Forced:
            status_.forced.add();
            return;
    }
}

void SaveStageDirector::publish_(const Facts& facts) noexcept {
    bool live = false;
    bool loading = false;
    bool holding = false;
    for (const Stage& st : stages_) {
        live = live || st.row.state == StageState::Live;
        loading = loading || st.row.state == StageState::Loading;
        holding = holding || st.watch.held.has_value();
    }
    status_.probing = facts.enabled
        && facts.declared
        && live
        && !loading
        && summary_.none_in_place();
    status_.holding = holding;
    if (published_ == status_)
        return;
    published_ = status_;
    status_cell_.publish(status_);
}

bool SaveStageDirector::open_window() noexcept {
    TASTY_SEAT_BODY(SaveStageDirector);
    if (!read_())
        return false;
    for (Stage& st : stages_) {
        const SaveStageRow& row = st.row;
        StageWatch& w = st.watch;
        if (w.key != row.key)
            w = StageWatch{.key = row.key, .commits_base = row.commits};
        w.at_open = row.state == StageState::Live ? std::optional{row.rev} : std::nullopt;
    }
    window_opened_ = clock_.now();
    return true;
}

void SaveStageDirector::cancel_window() noexcept {
    TASTY_SEAT_BODY(SaveStageDirector);
    window_opened_.reset();
    for (Stage& st : stages_)
        st.watch.at_open.reset();
}

void SaveStageDirector::release_holds() noexcept {
    TASTY_SEAT_BODY(SaveStageDirector);
    for (Stage& st : stages_)
        st.watch.held.reset();
}

proto::SaveBackup SaveStageDirector::backup_for_(
    const StageWatch& w,
    const SaveStageRow& row,
    const Facts& facts
) noexcept {
    const bool first = row.commits == w.commits_base;
    return facts.enabled && row.present && first ? proto::SaveBackup::SessionStart
                                                 : proto::SaveBackup::None;
}

bool SaveStageDirector::order_first_commits() noexcept {
    TASTY_SEAT_BODY(SaveStageDirector);
    if (!read_())
        return false;
    rows_();
    results_(clock_.now());
    for (Stage& st : stages_) {
        const SaveStageRow& row = st.row;
        StageWatch& w = st.watch;
        if (!resident(row) || !unsaved(row) || w.ordered)
            continue;
        if (backup_for_(w, row, facts_) == proto::SaveBackup::None)
            continue;
        order_(w, row, Cause::Forced, facts_);
    }
    return true;
}

bool SaveStageDirector::order_commit_all(proto::CommitAllSeq seq) noexcept {
    TASTY_SEAT_BODY(SaveStageDirector);
    if (!order_first_commits())
        return false;
    const proto::SaveStageOrder::CommitAll a{.seq = seq};
    if (orders_.push(infra::make<proto::SaveStageOrder>(a)))
        return true;
    tally_.orders_refused.add();
    return false;
}

bool SaveStageDirector::commit_all_served(proto::CommitAllSeq seq) const noexcept {
    TASTY_SEAT_BODY(SaveStageDirector);
    proto::SaveStageReport r{};
    return report_cell_.sample_into(r) != 0 && r.summary.commit_all_served == seq;
}

std::optional<infra::ParkTimeout> SaveStageDirector::next_due() const noexcept {
    TASTY_SEAT_BODY(SaveStageDirector);

    std::optional<infra::ParkTimeout> probe{};
    if (enabled_ && due_) {
        const auto left = *due_ - clock_.now();
        if (left > TimePoint::duration::zero())
            probe = std::chrono::ceil<infra::ParkTimeout>(left);
    }
    const auto join = [&probe](infra::ParkTimeout p) {
        return probe ? std::min(*probe, p) : p;
    };

    for (const Stage& st : stages_) {
        const StageWatch& w = st.watch;
        const bool backing_off = !w.ordered && w.retry_at && clock_.now() < *w.retry_at;
        if ((w.ordered || unsaved(st.row)) && !w.held && !backing_off)
            return join(kUserPassWake);
    }
    if (window_opened_)
        return join(kSettleWake);

    for (const Stage& st : stages_) {
        if (st.watch.ordered || unsaved(st.row) || st.row.state == StageState::Live)
            return join(kSettleWake);
    }
    if (probe)
        return *probe;
    return std::nullopt;
}

}
