// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/file_slot_backend.h"

#include <cerrno>
#include <string_view>
#include <utility>

#include "infra/byte_view.h"
#include "infra/errno.h"
#include "infra/path_parts.h"
#include "infra/prelude.h"
#include "svc/save_stage_keeper.h"
#include "svc/staged_save.h"
#include "svc/vfs.h"

namespace mister::svc {
namespace {

template <std::size_t... I>
SaveSlotBackends make_backends(
    std::index_sequence<I...>,
    const Vfs& vfs,
    SaveStageKeeper& keeper
) noexcept {
    return SaveSlotBackends{((void)I, FileSlotBackend{vfs, keeper})...};
}

std::unexpected<Error> no_file(infra::ErrorSite at) noexcept {
    return std::unexpected(Error{Errc::not_found, at, 0});
}

std::unexpected<Error> absent(infra::ErrorSite at) noexcept {
    return std::unexpected(infra::Errno{ENOENT}.refusal(Errc::not_found, at));
}

}

SaveSlotBackends make_save_slot_backends(const Vfs& vfs, SaveStageKeeper& keeper) noexcept {
    return make_backends(std::make_index_sequence<proto::kAnnounceableSlots>{}, vfs, keeper);
}

FileSlotBackend::FileSlotBackend(const Vfs& vfs, SaveStageKeeper& keeper) noexcept
    : keeper_(keeper),
      in_place_(vfs),
      staged_(keeper),
      file_(in_place_) {}

void FileSlotBackend::close_() noexcept {
    file_.get().close();
    file_ = in_place_;
}

Ex<proto::FileSize> FileSlotBackend::open_extent(
    proto::SlotIndex,
    const BlockCtl::StorageOrder& order
) {
    TASTY_SEAT_BODY(FileSlotBackend);
    close_();
    if (!in_place_.take_path(order))
        return no_file(ERR_SITE());
    in_place_.make_parent();
    return in_place_.open_file();
}

Ex<proto::FileSize> FileSlotBackend::open_save_extent(
    proto::SlotIndex,
    const BlockCtl::StorageOrder& order
) {
    TASTY_SEAT_BODY(FileSlotBackend);
    close_();
    if (!in_place_.take_path(order))
        return no_file(ERR_SITE());
    in_place_.make_parent();
    auto grant = keeper_.adopt(in_place_.path());
    if (!grant)
        return std::unexpected(grant.error());
    if (!grant->has_value()) {
        in_place_.counted_by(keeper_);
        return in_place_.open_file();
    }
    staged_.hold((*grant)->key);
    file_ = staged_;
    if (!(*grant)->extent)
        return absent(ERR_SITE());
    return *(*grant)->extent;
}

Ex<infra::ByteCount> FileSlotBackend::read_at(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(FileSlotBackend);
    return file_.get().read_at(at, dst);
}

Ex<infra::ByteCount> FileSlotBackend::write_at(
    proto::FileOffset at,
    std::span<const std::byte> src
) {
    TASTY_SEAT_BODY(FileSlotBackend);
    auto n = file_.get().write_at(at, src);
    if (!n && staged_.outgrown(n.error()))
        return demote_(at, src);
    return n;
}

Ex<infra::ByteCount> FileSlotBackend::demote_(
    proto::FileOffset at,
    std::span<const std::byte> src
) {
    if (auto d = staged_.demote(); !d)
        return std::unexpected(d.error());
    file_ = in_place_;
    in_place_.counted_by(keeper_);

    if (auto e = in_place_.open_file(); !e && e.error().code != Errc::not_found)
        return std::unexpected(e.error());
    return in_place_.write_at(at, src);
}

Ex<proto::FileSize> FileSlotBackend::create(std::span<const std::byte> first) {
    TASTY_SEAT_BODY(FileSlotBackend);
    return file_.get().create(first);
}

Ex<void> FileSlotBackend::flush() {
    TASTY_SEAT_BODY(FileSlotBackend);
    return file_.get().flush();
}

void FileSlotBackend::release(proto::SlotIndex) noexcept {
    TASTY_SEAT_BODY(FileSlotBackend);
    close_();
}

bool FileSlotBackend::InPlace::take_path(const BlockCtl::StorageOrder& order) noexcept {
    TASTY_SEAT_BODY(InPlace);

    if (!path_.assign(infra::as_text(order.view())) || path_.view().empty()) {
        path_.clear();
        return false;
    }
    return true;
}

void FileSlotBackend::InPlace::make_parent() {
    TASTY_SEAT_BODY(InPlace);

    if (const std::optional<std::string_view> dir = infra::nested_dir(path_.view())) {
        (void)card_.ensure_dir(*dir);
    }
}

Ex<proto::FileSize> FileSlotBackend::InPlace::open_file() {
    TASTY_SEAT_BODY(InPlace);
    file_.reset();
    auto f = card_.open(path_.view(), OpenMode::ReadWrite);

    if (!f)
        return std::unexpected(f.error());
    auto size = (*f)->size();
    if (!size)
        return std::unexpected(size.error());
    file_ = std::move(*f);
    return *size;
}

void FileSlotBackend::InPlace::counted_by(SaveStageKeeper& keeper) noexcept {
    TASTY_SEAT_BODY(InPlace);
    keeper.note_in_place_opened();
    counted_by_ = infra::OptRef<SaveStageKeeper>{keeper};
}

Ex<infra::ByteCount> FileSlotBackend::InPlace::read_at(
    proto::FileOffset at,
    std::span<std::byte> dst
) {
    TASTY_SEAT_BODY(InPlace);
    if (file_ == nullptr)
        return no_file(ERR_SITE());
    auto n = file_->read_at(at, dst);
    if (!n)
        return std::unexpected(n.error());
    return infra::ByteCount{*n};
}

Ex<infra::ByteCount> FileSlotBackend::InPlace::write_at(
    proto::FileOffset at,
    std::span<const std::byte> src
) {
    TASTY_SEAT_BODY(InPlace);
    if (file_ == nullptr)
        return no_file(ERR_SITE());
    auto n = file_->write_at(at, src);

    if (n) {
        if (auto r = file_->flush(); !r)
            return std::unexpected(r.error());
    }
    if (!n)
        return std::unexpected(n.error());
    return infra::ByteCount{*n};
}

Ex<proto::FileSize> FileSlotBackend::InPlace::create(std::span<const std::byte> first) {
    TASTY_SEAT_BODY(InPlace);
    const std::string_view path = path_.view();
    if (path.empty())
        return no_file(ERR_SITE());

    auto f = card_.open(path, OpenMode::Create);
    if (!f)
        return std::unexpected(f.error());
    if (auto w = (*f)->write_all_at(proto::kFileStart, first); !w) {
        return std::unexpected(w.error());
    }
    if (auto r = (*f)->flush(); !r)
        return std::unexpected(r.error());
    card_.sync_parent_dir(path);
    file_ = std::move(*f);
    return proto::FileSize{first.size()};
}

Ex<void> FileSlotBackend::InPlace::flush() {
    TASTY_SEAT_BODY(InPlace);
    if (file_ == nullptr)
        return {};
    return file_->flush();
}

void FileSlotBackend::InPlace::close() noexcept {
    TASTY_SEAT_BODY(InPlace);
    file_.reset();
    if (counted_by_)
        counted_by_->note_in_place_closed();
    counted_by_ = {};
}

void FileSlotBackend::Staged::hold(proto::StageKey key) noexcept {
    TASTY_SEAT_BODY(Staged);
    key_ = key;
}

bool FileSlotBackend::Staged::outgrown(const Error& e) const noexcept {
    TASTY_SEAT_BODY(Staged);
    return key_.has_value() && StagedSave::is_too_large(e);
}

Ex<void> FileSlotBackend::Staged::demote() {
    TASTY_SEAT_BODY(Staged);
    if (!key_)
        return no_file(ERR_SITE());
    if (auto d = keeper_.demote(*key_); !d)
        return d;
    key_.reset();
    return {};
}

Ex<infra::ByteCount> FileSlotBackend::Staged::read_at(
    proto::FileOffset at,
    std::span<std::byte> dst
) {
    TASTY_SEAT_BODY(Staged);
    if (!key_)
        return no_file(ERR_SITE());
    return keeper_.read(*key_, at, dst);
}

Ex<infra::ByteCount> FileSlotBackend::Staged::write_at(
    proto::FileOffset at,
    std::span<const std::byte> src
) {
    TASTY_SEAT_BODY(Staged);
    if (!key_)
        return no_file(ERR_SITE());
    return keeper_.write(*key_, at, src);
}

Ex<proto::FileSize> FileSlotBackend::Staged::create(std::span<const std::byte> first) {
    TASTY_SEAT_BODY(Staged);
    if (!key_)
        return no_file(ERR_SITE());
    return keeper_.create(*key_, first);
}

Ex<void> FileSlotBackend::Staged::flush() {
    TASTY_SEAT_BODY(Staged);
    return {};
}

void FileSlotBackend::Staged::close() noexcept {
    TASTY_SEAT_BODY(Staged);
    if (key_)
        keeper_.retire(*key_);
    key_.reset();
}

}
