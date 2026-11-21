// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/save_stage_keeper.h"

#include <algorithm>
#include <cerrno>
#include <optional>
#include <utility>

#include "infra/errno.h"
#include "infra/message_sum.h"
#include "infra/path_parts.h"
#include "infra/prelude.h"
#include "infra/span_cut.h"
#include "svc/vfs.h"

namespace mister::svc {

using proto::FileSize;
using proto::SaveBackup;
using proto::SaveStageSlot;
using proto::StageState;

namespace {

template <std::size_t... I>
consteval std::array<SaveStageSlot, sizeof...(I)> slots_of(std::index_sequence<I...>) {
    return {SaveStageSlot{I}...};
}

constexpr auto kSlots = slots_of(std::make_index_sequence<proto::kMaxStagedSaves.size()>{});

bool errno_is(const Error& e, infra::Errno want) noexcept {
    return e.code == Errc::os && e.os_errno() == want;
}

bool no_space(const Error& e) noexcept {
    return errno_is(e, infra::Errno{ENOSPC}) || errno_is(e, infra::Errno{EDQUOT});
}

std::unexpected<Error> stale_key(infra::ErrorSite at) noexcept {
    return std::unexpected(Error{Errc::stale, at, 0});
}

std::optional<SlotPathText> temp_beside(std::string_view path) noexcept {
    SlotPathText t{};
    if (!t.assign(infra::dir_prefix(path)) || !t.append(SaveStageKeeper::kTempName))
        return std::nullopt;
    return t;
}

bool committable(std::string_view path) noexcept {
    SlotPathText own{};
    return own.assign(path) && temp_beside(path).has_value();
}

}

SaveStageKeeper::SaveStageKeeper(
    const Vfs& vfs,
    const os::IClock& clock,
    SaveStageOrders& orders,
    SaveStageCell& report
)
    : vfs_(vfs),
      card_(vfs),
      clock_(clock),
      orders_(orders),
      cell_(report),
      commit_buf_(proto::kStagedSaveMaxBytes.zeroed<std::vector<std::byte>>()),
      chunk_buf_(proto::kCommitChunkBytes.zeroed<std::vector<std::byte>>()) {}

infra::OptRef<StagedSave> SaveStageKeeper::staged_(proto::StageKey key) noexcept {
    auto st = pool_.find(key.slot);
    if (!st || !st->holds(key))
        return {};
    return st;
}

std::optional<SaveStageSlot> SaveStageKeeper::free_slot_() const noexcept {
    for (const SaveStageSlot s : kSlots)
        if (pool_[s].state() == StageState::Free)
            return s;
    return std::nullopt;
}

bool SaveStageKeeper::live_path_(std::string_view path) const noexcept {
    for (const StagedSave& st : pool_)
        if (st.state() != StageState::Free && st.path() == path)
            return true;
    return false;
}

std::optional<SaveStageSlot> SaveStageKeeper::next_unsaved_(
    std::optional<SaveStageSlot> after
) const noexcept {
    for (const SaveStageSlot s : kSlots) {
        if (after && s <= *after)
            continue;
        if (pool_[s].resident() && pool_[s].unsaved())
            return s;
    }
    return std::nullopt;
}

bool SaveStageKeeper::loading_() const noexcept {
    for (const StagedSave& st : pool_)
        if (st.state() == StageState::Loading)
            return true;
    return false;
}

bool SaveStageKeeper::busy_() const noexcept {
    return job_.has_value() || !orders_.empty() || loading_();
}

Ex<std::optional<SaveStageKeeper::Grant>> SaveStageKeeper::adopt(std::string_view path) {
    TASTY_SEAT_BODY(SaveStageKeeper);
    for (const SaveStageSlot s : kSlots) {
        StagedSave& st = pool_[s];
        if (st.state() == StageState::Retiring && st.path() == path) {
            st.revive();
            publish_();
            return Grant{st.key(), st.announced()};
        }
    }

    if (live_path_(path))
        return std::unexpected(infra::Errno{EBUSY}.refusal(Errc::os, ERR_SITE()));
    const std::optional<SaveStageSlot> slot = free_slot_();
    if (!slot || !committable(path))
        return decline_();
    StagedSave& st = pool_[*slot];
    const proto::StageKey key{st.key().epoch.next(), *slot};

    auto f = card_.open(path, OpenMode::ReadWrite);
    if (!f) {
        if (f.error().code != Errc::not_found)
            return std::unexpected(f.error());
        if (vfs_.name_kind(path) != Vfs::NameKind::Absent)
            return decline_();
        st.take_absent(key, path);
        publish_();
        return Grant{key, std::nullopt};
    }
    if (vfs_.name_kind(path) != Vfs::NameKind::PlainFile)
        return decline_();
    auto size = (*f)->size();
    if (!size)
        return std::unexpected(size.error());
    if (*size > proto::kStagedSaveMaxBytes)
        return decline_();
    st.take_loading(key, path, std::move(*f), *size);
    publish_();
    return Grant{key, *size};
}

std::optional<SaveStageKeeper::Grant> SaveStageKeeper::decline_() noexcept {
    summary_.unstaged_adopts.add();
    publish_();
    return std::nullopt;
}

Ex<infra::ByteCount> SaveStageKeeper::read(
    proto::StageKey key,
    proto::FileOffset at,
    std::span<std::byte> dst
) {
    TASTY_SEAT_BODY(SaveStageKeeper);
    auto st = staged_(key);
    if (!st || st->state() == StageState::Retiring)
        return stale_key(ERR_SITE());
    return st->read(at, dst);
}

Ex<infra::ByteCount> SaveStageKeeper::write(
    proto::StageKey key,
    proto::FileOffset at,
    std::span<const std::byte> src
) {
    TASTY_SEAT_BODY(SaveStageKeeper);
    auto st = staged_(key);
    if (!st || st->state() == StageState::Retiring)
        return stale_key(ERR_SITE());
    if (st->state() == StageState::Loading) {
        st->finish_load();
        summary_.loads_rushed.add();
    }
    auto r = st->write(at, src, clock_.now());
    publish_();
    return r;
}

Ex<FileSize> SaveStageKeeper::create(proto::StageKey key, std::span<const std::byte> first) {
    TASTY_SEAT_BODY(SaveStageKeeper);
    auto st = staged_(key);
    if (!st || st->state() == StageState::Retiring)
        return stale_key(ERR_SITE());
    if (st->state() == StageState::Loading) {
        st->finish_load();
        summary_.loads_rushed.add();
    }
    auto r = st->create(first, clock_.now());
    publish_();
    return r;
}

Ex<void> SaveStageKeeper::commit_image(std::string_view path, std::span<const std::byte> image) {
    TASTY_SEAT_BODY(SaveStageKeeper);
    return card_.replace_file(path, image);
}

void SaveStageKeeper::retire(proto::StageKey key) noexcept {
    TASTY_SEAT_BODY(SaveStageKeeper);
    auto st = staged_(key);
    if (!st)
        return;
    st->retire();
    const bool job_on_it = job_ && job_->slot == key.slot;
    if (!st->unsaved() && !job_on_it)
        st->release();
    publish_();
}

Ex<void> SaveStageKeeper::demote(proto::StageKey key) {
    TASTY_SEAT_BODY(SaveStageKeeper);
    auto st = staged_(key);
    if (!st)
        return stale_key(ERR_SITE());

    const std::optional<proto::CommitAllSeq> sweep = std::exchange(sweep_, std::nullopt);
    const std::optional<SaveStageSlot> cursor = job_ ? std::optional<SaveStageSlot>{job_->slot}
                                                     : std::nullopt;
    finish_inline_();
    st->finish_load();
    if (st->resident() && st->unsaved()) {
        start_(key.slot, SaveBackup::None);
        finish_inline_();
    }
    const bool landed = !st->unsaved();
    if (landed) {
        st->release();
        summary_.demotions.add();
    }
    sweep_ = sweep;
    if (sweep_)
        sweep_from_(cursor);
    publish_();
    if (!landed)
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    return {};
}

void SaveStageKeeper::note_in_place_opened() noexcept {
    TASTY_SEAT_BODY(SaveStageKeeper);
    summary_.in_place.opened();
    publish_();
}

void SaveStageKeeper::note_in_place_closed() noexcept {
    TASTY_SEAT_BODY(SaveStageKeeper);
    if (!summary_.in_place.closed())
        summary_.in_place_unmatched.add();
    publish_();
}

void SaveStageKeeper::serve() noexcept {
    TASTY_SEAT_BODY(SaveStageKeeper);
    if (loading_()) {
        step_one_load_();
    } else if (job_) {
        step_job_();
    } else if (auto o = orders_.pop()) {
        take_order_(*o);
    } else {
        return;
    }
    publish_();
}

bool SaveStageKeeper::idle() const noexcept {
    TASTY_SEAT_BODY(SaveStageKeeper);
    return !busy_();
}

bool SaveStageKeeper::quiescent() const noexcept {
    TASTY_SEAT_BODY(SaveStageKeeper);
    if (paused_at_ && clock_.now() - *paused_at_ >= proto::kSavePauseDrain)
        return true;
    return !busy_();
}

void SaveStageKeeper::on_pause() noexcept {
    TASTY_SEAT_BODY(SaveStageKeeper);
    paused_at_ = clock_.now();
}

void SaveStageKeeper::on_resume() noexcept {
    TASTY_SEAT_BODY(SaveStageKeeper);
    paused_at_.reset();
}

void SaveStageKeeper::on_stop() noexcept {
    TASTY_SEAT_BODY(SaveStageKeeper);
    for (;;) {
        finish_inline_();
        const std::optional<proto::SaveStageOrder> o = orders_.pop();
        if (!o)
            break;
        take_order_(*o);
    }
    sweep_inline_();
    publish_();
}

void SaveStageKeeper::step_one_load_() noexcept {
    for (StagedSave& st : pool_) {
        if (st.state() == StageState::Loading) {
            st.step_load();
            return;
        }
    }
}

void SaveStageKeeper::take_order_(const proto::SaveStageOrder& o) noexcept {
    infra::dispatch<infra::AllRouted>(o, *this);
}

void SaveStageKeeper::on(const proto::SaveStageOrder::Commit& c) noexcept {
    TASTY_SEAT_BODY(SaveStageKeeper);
    auto st = staged_(c.key);
    if (!st || !st->resident()) {
        summary_.orders_stale.add();
        return;
    }
    if (st->rev() != c.rev) {
        st->note_decline();
        return;
    }
    if (st->unsaved())
        start_(c.key.slot, c.backup);
}

void SaveStageKeeper::on(const proto::SaveStageOrder::CommitAll& a) noexcept {
    TASTY_SEAT_BODY(SaveStageKeeper);
    sweep_ = a.seq;
    if (const auto first = next_unsaved_(std::nullopt)) {
        start_(*first, SaveBackup::None);
        return;
    }
    summary_.commit_all_served = a.seq;
    sweep_.reset();
}

void SaveStageKeeper::misrouted(const proto::SaveStageOrder&) noexcept {
    TASTY_SEAT_BODY(SaveStageKeeper);
    summary_.orders_stale.add();
}

void SaveStageKeeper::start_(SaveStageSlot slot, SaveBackup backup) noexcept {
    StagedSave& st = pool_[slot];
    Job j{};
    j.slot = slot;
    j.rev = st.rev();
    j.backup = backup;
    j.size = st.extent();
    std::ranges::copy(st.image(), commit_buf_.begin());
    job_ = std::move(j);
    const std::optional<SlotPathText> temp = temp_beside(st.path());
    if (!temp) {
        fail_(st, Error{Errc::bad_format, ERR_SITE(), 0});
        return;
    }
    job_->temp = *temp;
    job_->step = Step::Write;
    if (!st.present())
        return;
    auto f = vfs_.open(st.path(), OpenMode::Read);
    if (!f)
        return;
    auto card = (*f)->size();
    if (!card)
        return;
    job_->card = *card;
    job_->src = std::move(*f);
    if (job_->card == job_->size) {
        job_->step = Step::Compare;
        return;
    }
    after_compare_(*job_);
}

void SaveStageKeeper::step_job_() noexcept {
    Job& j = *job_;
    StagedSave& st = pool_[j.slot];
    switch (j.step) {
        case Step::Compare:
            step_compare_(j, st);
            return;
        case Step::Backup:
            step_backup_(j, st);
            return;
        case Step::Write:
            step_write_(j, st);
            return;
        case Step::WriteInPlace:
            step_in_place_(j, st);
            return;
        case Step::Replace:
            step_replace_(j, st);
            return;
    }
}

void SaveStageKeeper::after_compare_(Job& j) noexcept {
    j.at = proto::kFileStart;
    if (j.backup == SaveBackup::SessionStart && j.src) {
        j.step = Step::Backup;
        return;
    }
    j.src.reset();
    j.step = Step::Write;
}

void SaveStageKeeper::step_compare_(Job& j, StagedSave& st) noexcept {
    const FileSize n = ((proto::kFileStart + j.size) - j.at).min(proto::kCommitChunkBytes);
    if (!n.empty()) {
        const std::span<std::byte> card = infra::affine::first(std::span(chunk_buf_), n);
        if (!j.src->read_exact_at(j.at, card)) {
            after_compare_(j);
            return;
        }
        if (!std::ranges::equal(
                card,
                infra::affine::first(infra::affine::from(std::span(commit_buf_), j.at), n)
            )) {
            after_compare_(j);
            return;
        }
        j.at += n;
    }
    if (j.at == proto::kFileStart + j.size) {
        st.found_identical(j.rev);
        end_job_();
    }
}

void SaveStageKeeper::step_backup_(Job& j, StagedSave& st) noexcept {
    if (!j.dst) {
        SlotPathText bak{};
        if (!bak.assign(st.path()) || !bak.append(kBackupSuffix))
            return skip_backup_(j);
        auto f = card_.open(bak.view(), OpenMode::Truncate);
        if (!f)
            return skip_backup_(j);
        j.dst = std::move(*f);
        j.at = proto::kFileStart;
    }
    const FileSize n = ((proto::kFileStart + j.card) - j.at).min(proto::kCommitChunkBytes);
    if (!n.empty()) {
        const std::span<std::byte> chunk = infra::affine::first(std::span(chunk_buf_), n);
        if (!j.src->read_exact_at(j.at, chunk))
            return skip_backup_(j);
        if (!j.dst->write_all_at(j.at, chunk))
            return skip_backup_(j);
        if (!j.dst->flush())
            return skip_backup_(j);
        j.at += n;
    }
    if (j.at == proto::kFileStart + j.card)
        to_write_(j);
}

void SaveStageKeeper::skip_backup_(Job& j) noexcept {
    summary_.backups_skipped.add();
    to_write_(j);
}

void SaveStageKeeper::to_write_(Job& j) noexcept {
    j.dst.reset();
    j.src.reset();
    j.at = proto::kFileStart;
    j.step = Step::Write;
}

void SaveStageKeeper::write_refused_(Job& j, StagedSave& st, const Error& e) noexcept {
    if (no_space(e) && st.present()) {
        summary_.in_place_fallbacks.add();
        j.dst.reset();
        (void)card_.open(j.temp.view(), OpenMode::Truncate);
        j.at = proto::kFileStart;
        j.step = Step::WriteInPlace;
        return;
    }
    fail_(st, e);
}

void SaveStageKeeper::step_write_(Job& j, StagedSave& st) noexcept {
    if (!j.dst) {
        j.src.reset();
        auto f = card_.open(j.temp.view(), OpenMode::Truncate);
        if (!f)
            return write_refused_(j, st, f.error());
        j.dst = std::move(*f);
        j.at = proto::kFileStart;
    }
    const FileSize n = ((proto::kFileStart + j.size) - j.at).min(proto::kCommitChunkBytes);
    if (!n.empty()) {
        const std::span<const std::byte> chunk = infra::affine::first(
            infra::affine::from(std::span<const std::byte>(commit_buf_), j.at),
            n
        );
        if (auto w = j.dst->write_all_at(j.at, chunk); !w)
            return write_refused_(j, st, w.error());
        if (auto s = j.dst->flush(); !s)
            return write_refused_(j, st, s.error());
        j.at += n;
    }
    if (j.at == proto::kFileStart + j.size) {
        j.dst.reset();
        j.step = Step::Replace;
    }
}

void SaveStageKeeper::step_in_place_(Job& j, StagedSave& st) noexcept {
    if (!j.dst) {
        auto f = card_.open(st.path(), OpenMode::ReadWrite);
        if (!f)
            return fail_(st, f.error());
        j.dst = std::move(*f);
        j.at = proto::kFileStart;
    }
    const FileSize n = ((proto::kFileStart + j.size) - j.at).min(proto::kCommitChunkBytes);
    if (!n.empty()) {
        const std::span<const std::byte> chunk = infra::affine::first(
            infra::affine::from(std::span<const std::byte>(commit_buf_), j.at),
            n
        );
        if (auto w = j.dst->write_all_at(j.at, chunk); !w)
            return fail_(st, w.error());
        if (auto s = j.dst->flush(); !s)
            return fail_(st, s.error());
        j.at += n;
    }
    if (j.at == proto::kFileStart + j.size)
        land_(st);
}

void SaveStageKeeper::step_replace_(Job& j, StagedSave& st) noexcept {
    if (auto r = card_.replace({j.temp.view(), st.path()}); !r)
        return fail_(st, r.error());
    land_(st);
}

void SaveStageKeeper::land_(StagedSave& st) noexcept {
    st.saved_as(job_->rev);
    end_job_();
}

void SaveStageKeeper::fail_(StagedSave& st, const Error& e) noexcept {
    st.note_fault(e);
    end_job_();
}

void SaveStageKeeper::end_job_() noexcept {
    const SaveStageSlot slot = job_->slot;
    job_.reset();
    StagedSave& st = pool_[slot];
    if (st.state() == StageState::Retiring && !st.unsaved())
        st.release();
    if (sweep_)
        sweep_from_(slot);
}

void SaveStageKeeper::sweep_from_(std::optional<SaveStageSlot> after) noexcept {
    if (const auto next = next_unsaved_(after)) {
        start_(*next, SaveBackup::None);
        return;
    }
    summary_.commit_all_served = *sweep_;
    sweep_.reset();
}

void SaveStageKeeper::finish_inline_() noexcept {
    while (job_)
        step_job_();
}

void SaveStageKeeper::sweep_inline_() noexcept {
    for (auto s = next_unsaved_(std::nullopt); s; s = next_unsaved_(s)) {
        start_(*s, SaveBackup::None);
        finish_inline_();
    }
}

proto::SaveStageReport SaveStageKeeper::report() const noexcept {
    proto::SaveStageReport r{.summary = summary_};
    for (const SaveStageSlot s : kSlots)
        r.rows[s] = pool_[s].row();
    return r;
}

void SaveStageKeeper::publish_() noexcept {
    cell_.publish(report());
}

}
