// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/staged_save.h"

#include <algorithm>
#include <cerrno>
#include <utility>

#include "infra/errno.h"
#include "infra/prelude.h"
#include "infra/span_cut.h"
#include "proto/autosave_policy.h"

namespace mister::svc {

using proto::FileSize;
using proto::StageState;

Error StagedSave::too_large(infra::ErrorSite at) noexcept {
    return infra::Errno{EFBIG}.refusal(Errc::os, at);
}

bool StagedSave::is_too_large(const Error& e) noexcept {
    return e.code == Errc::os && e.os_errno() == infra::Errno{EFBIG};
}

void StagedSave::take_(proto::StageKey key, std::string_view path) {
    image_.clear();
    key_ = key;
    rev_ = proto::StageRevision{};
    saved_ = proto::StageRevision{};
    loaded_ = proto::kFileStart;
    broken_.reset();
    load_.reset();
    if (!path_.assign(path))
        path_.clear();
}

void StagedSave::take_absent(proto::StageKey key, std::string_view path) {
    TASTY_SEAT_BODY(StagedSave);
    take_(key, path);
    present_ = false;
    proto::kStageFirstReserve.reserve_in(image_);
    state_ = StageState::Live;
}

void StagedSave::take_loading(
    proto::StageKey key,
    std::string_view path,
    std::unique_ptr<IFile> file,
    FileSize size
) {
    TASTY_SEAT_BODY(StagedSave);
    take_(key, path);
    present_ = true;
    size.resize_in(image_);
    load_ = std::move(file);
    state_ = StageState::Loading;
    if (size.empty())
        step_load();
}

void StagedSave::step_load() noexcept {
    TASTY_SEAT_BODY(StagedSave);
    if (state_ != StageState::Loading)
        return;
    const FileSize left = (proto::kFileStart + extent()) - loaded_;
    if (!left.empty()) {
        const FileSize n = left.min(proto::kCommitChunkBytes);
        const std::span<std::byte> dst = infra::affine::first(
            infra::affine::from(std::span(image_), loaded_),
            n
        );
        if (auto r = load_->read_exact_at(loaded_, dst); !r) {
            break_(r.error());
            return;
        }
        loaded_ += n;
    }
    if (loaded_ == proto::kFileStart + extent()) {
        load_.reset();
        state_ = StageState::Live;
    }
}

void StagedSave::finish_load() noexcept {
    TASTY_SEAT_BODY(StagedSave);
    while (state_ == StageState::Loading)
        step_load();
}

void StagedSave::break_(Error e) noexcept {
    load_.reset();
    image_.clear();
    broken_ = e;
    state_ = StageState::Broken;
}

void StagedSave::retire() noexcept {
    TASTY_SEAT_BODY(StagedSave);
    load_.reset();
    state_ = StageState::Retiring;
}

void StagedSave::revive() noexcept {
    TASTY_SEAT_BODY(StagedSave);
    state_ = StageState::Live;
}

void StagedSave::release() noexcept {
    TASTY_SEAT_BODY(StagedSave);
    image_ = {};
    load_.reset();
    broken_.reset();
    path_.clear();
    state_ = StageState::Free;
}

void StagedSave::saved_as(proto::StageRevision rev) noexcept {
    TASTY_SEAT_BODY(StagedSave);
    saved_ = rev;
    present_ = true;
    commits_.add();
}

void StagedSave::found_identical(proto::StageRevision rev) noexcept {
    TASTY_SEAT_BODY(StagedSave);
    saved_ = rev;
    skipped_identical_.add();
}

bool StagedSave::holds(proto::StageKey key) const noexcept {
    return state_ != StageState::Free && key_ == key;
}

bool StagedSave::resident() const noexcept {
    return state_ == StageState::Live || state_ == StageState::Retiring;
}

FileSize StagedSave::extent() const noexcept {
    return FileSize::of(std::span(image_));
}

std::optional<FileSize> StagedSave::announced() const noexcept {
    if (!present_ && image_.empty())
        return std::nullopt;
    return extent();
}

Ex<infra::ByteCount> StagedSave::read(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(StagedSave);
    if (broken_)
        return std::unexpected(*broken_);
    if (state_ == StageState::Loading) {
        auto n = load_->read_at(at, dst);
        if (!n)
            return std::unexpected(n.error());
        return infra::ByteCount{*n};
    }
    if (!announced())
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (!extent().holds(at))
        return infra::ByteCount{0};
    const std::span<const std::byte> from = infra::affine::from(image(), at);
    const std::size_t n = std::min(from.size(), dst.size());
    std::copy_n(from.begin(), n, dst.begin());
    return infra::ByteCount{n};
}

Ex<infra::ByteCount> StagedSave::write(
    proto::FileOffset at,
    std::span<const std::byte> src,
    TimePoint now
) {
    TASTY_SEAT_BODY(StagedSave);
    if (broken_)
        return std::unexpected(*broken_);
    const FileSize end = (at - proto::kFileStart) + FileSize::of(src);
    if (end > proto::kStagedSaveMaxBytes)
        return std::unexpected(too_large(ERR_SITE()));
    last_write_ = now;
    if (end <= extent()) {
        const std::span<std::byte> dst = infra::affine::from(std::span(image_), at)
                                             .first(src.size());
        if (std::ranges::equal(dst, src))
            return infra::ByteCount{src.size()};
        std::ranges::copy(src, dst.begin());
    } else {
        end.resize_in(image_);
        std::ranges::copy(src, infra::affine::from(std::span(image_), at).begin());
    }
    rev_ = rev_.next();
    return infra::ByteCount{src.size()};
}

Ex<FileSize> StagedSave::create(std::span<const std::byte> first, TimePoint now) {
    TASTY_SEAT_BODY(StagedSave);
    auto w = write(proto::kFileStart, first, now);
    if (!w)
        return std::unexpected(w.error());

    if (!present_ && !unsaved())
        rev_ = rev_.next();
    return FileSize::of(*w);
}

proto::SaveStageRow StagedSave::row() const noexcept {
    proto::SaveStageRow r{};
    r.key = key_;
    r.state = state_;
    r.present = present_;
    r.rev = rev_;
    r.saved = saved_;
    r.last_write = last_write_;
    r.commits = commits_;
    r.skipped_identical = skipped_identical_;
    r.faults = faults_;
    r.last_fault = last_fault_;
    r.declines = declines_;
    return r;
}

}
