// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/file_slot_backend.h"

#include <cerrno>
#include <string_view>

#include "infra/byte_view.h"
#include "infra/path_parts.h"
#include "svc/vfs.h"

namespace mister::svc {

namespace {

std::span<std::byte> as_bytes_mut(std::span<std::uint8_t> s) noexcept {
    return infra::byte_cast<std::byte>(s);
}

std::span<const std::byte> as_bytes_const(std::span<const std::uint8_t> s) noexcept {
    return infra::byte_cast<const std::byte>(s);
}

}

Ex<proto::FileSize> FileSlotBackend::open_extent(
    proto::SlotIndex,
    const BlockCtl::StorageOrder& order
) {
    TASTY_SEAT_BODY(FileSlotBackend);
    file_.reset();
    path_.clear();

    if (!path_.assign(infra::as_text(order.view())) || path_.view().empty()) {
        path_.clear();
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    const std::string_view path = path_.view();

    if (const std::optional<std::string_view> dir = infra::nested_dir(path)) {
        (void)vfs_.ensure_dir(*dir);
    }
    auto f = vfs_.open(path, OpenMode::ReadWrite);
    if (!f) {

        if (f.error().code == Errc::os && f.error().detail == ENOENT) {
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), ENOENT});
        }
        return std::unexpected(f.error());
    }
    auto size = (*f)->size();
    if (!size)
        return std::unexpected(size.error());
    file_ = std::move(*f);
    return *size;
}

Ex<std::span<std::uint8_t>> FileSlotBackend::read_at(
    proto::FileSize at,
    std::span<std::uint8_t> dst
) {
    TASTY_SEAT_BODY(FileSlotBackend);
    if (file_ == nullptr)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    auto n = file_->read_at(at, as_bytes_mut(dst));
    if (!n)
        return std::unexpected(n.error());
    return dst.first(*n);
}

Ex<std::span<const std::uint8_t>> FileSlotBackend::write_at(
    proto::FileSize at,
    std::span<const std::uint8_t> src
) {
    TASTY_SEAT_BODY(FileSlotBackend);
    if (file_ == nullptr)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    auto n = file_->write_at(at.file_word<std::uint64_t>(), as_bytes_const(src));

    if (n) {
        if (auto r = file_->flush(); !r)
            return std::unexpected(r.error());
    }
    if (!n)
        return std::unexpected(n.error());
    return src.first(*n);
}

Ex<proto::FileSize> FileSlotBackend::create(std::span<const std::uint8_t> first) {
    TASTY_SEAT_BODY(FileSlotBackend);
    const std::string_view path = path_.view();
    if (path.empty())
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});

    auto f = vfs_.open(path, OpenMode::Create);
    if (!f)
        return std::unexpected(f.error());
    auto n = (*f)->write_at(0, as_bytes_const(first));
    if (!n)
        return std::unexpected(n.error());
    if (*n != first.size()) {
        return std::unexpected(
            Error{Errc::short_write, ERR_SITE(), static_cast<std::uint32_t>(*n)}
        );
    }
    if (auto r = (*f)->flush(); !r)
        return std::unexpected(r.error());
    vfs_.sync_parent_dir(path);
    file_ = std::move(*f);
    return proto::FileSize{first.size()};
}

Ex<void> FileSlotBackend::flush() {
    TASTY_SEAT_BODY(FileSlotBackend);
    if (file_ == nullptr)
        return {};
    return file_->flush();
}

void FileSlotBackend::release(proto::SlotIndex) noexcept {
    TASTY_SEAT_BODY(FileSlotBackend);
    file_.reset();
}

}
