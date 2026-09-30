// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/raw_disk_image.h"

#include <cerrno>

namespace mister::svc {

FileSize RawDiskImage::size() const noexcept {
    TASTY_SEAT_BODY(RawDiskImage);
    return size_;
}

bool RawDiskImage::writable() const noexcept {
    TASTY_SEAT_BODY(RawDiskImage);
    return writable_;
}

Ex<std::size_t> RawDiskImage::read_at(std::uint64_t off, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(RawDiskImage);
    return file_->read_at(off, dst);
}

Ex<std::size_t> RawDiskImage::write_at(std::uint64_t off, std::span<const std::byte> src) {
    TASTY_SEAT_BODY(RawDiskImage);
    if (!writable_) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EROFS)});
    }
    auto n = file_->write_at(off, src);
    if (!n) return n;
    if (auto f = file_->flush(); !f) return std::unexpected(f.error());
    return n;
}

}  // namespace mister::svc
