// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/file_slot_backend.h"

#include <cerrno>
#include <string_view>

namespace mister::svc {

namespace {

std::span<std::byte> as_bytes_mut(std::span<std::uint8_t> s) noexcept {
    return std::span<std::byte>(reinterpret_cast<std::byte*>(s.data()), s.size());
}

std::span<const std::byte> as_bytes_const(std::span<const std::uint8_t> s) noexcept {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(s.data()), s.size());
}

}  // namespace

Ex<proto::FileSize> FileSlotBackend::open_extent() {
    TASTY_SEAT_BODY(FileSlotBackend);
    file_.reset();
    const std::string_view path = path_->path.view();
    if (path.empty()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }

    if (const std::size_t slash = path.rfind('/'); slash != std::string_view::npos && slash > 0) {
        (void)vfs_->ensure_dir(path.substr(0, slash));
    }
    auto f = vfs_->open(path, OpenMode::ReadWrite);
    if (!f) {

        if (f.error().code == Errc::os && f.error().detail == ENOENT) {
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), ENOENT});
        }
        return std::unexpected(f.error());
    }
    auto size = (*f)->size();
    if (!size) return std::unexpected(size.error());
    file_ = std::move(*f);
    return *size;
}

Ex<std::size_t> FileSlotBackend::read_at(std::uint64_t offset, std::span<std::uint8_t> dst) {
    TASTY_SEAT_BODY(FileSlotBackend);
    if (file_ == nullptr) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    return file_->read_at(offset, as_bytes_mut(dst));
}

Ex<std::size_t> FileSlotBackend::write_at(std::uint64_t offset, std::span<const std::uint8_t> src) {
    TASTY_SEAT_BODY(FileSlotBackend);
    if (file_ == nullptr) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    auto n = file_->write_at(offset, as_bytes_const(src));

    if (n) {
        if (auto r = file_->flush(); !r) return std::unexpected(r.error());
    }
    return n;
}

Ex<proto::FileSize> FileSlotBackend::create(std::span<const std::uint8_t> first) {
    TASTY_SEAT_BODY(FileSlotBackend);
    const std::string_view path = path_->path.view();
    if (path.empty()) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});

    auto f = vfs_->open(path, OpenMode::Create);
    if (!f) return std::unexpected(f.error());
    auto n = (*f)->write_at(0, as_bytes_const(first));
    if (!n) return std::unexpected(n.error());
    if (*n != first.size()) {
        return std::unexpected(
            Error{Errc::short_write, ERR_SITE(), static_cast<std::uint32_t>(*n)});
    }
    if (auto r = (*f)->flush(); !r) return std::unexpected(r.error());
    file_ = std::move(*f);
    return proto::FileSize{first.size()};
}

Ex<void> FileSlotBackend::flush() {
    TASTY_SEAT_BODY(FileSlotBackend);
    if (file_ == nullptr) return {};
    return file_->flush();
}

void FileSlotBackend::release() noexcept {
    TASTY_SEAT_BODY(FileSlotBackend);
    file_.reset();
}

}  // namespace mister::svc
