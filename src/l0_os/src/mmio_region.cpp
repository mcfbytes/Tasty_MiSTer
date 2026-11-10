// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/mmio_region.h"

#include "infra/errno.h"
#include "infra/persist.h"
#include "os/map_length.h"
#include "os/stack_touch.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <utility>

namespace mister::os {
namespace {

constexpr int kDevMemFlags = O_RDWR | O_SYNC | O_CLOEXEC;

static_assert(sizeof(off_t) >= 8, "MmioRegion needs a 64-bit off_t");

struct DevMem {
    int fd = -1;
    infra::Errno open_errno{};

    DevMem() {
        fd = ::open("/dev/mem", kDevMemFlags);
        if (fd < 0)
            open_errno = infra::Errno{errno};
    }
};

const DevMem& dev_mem() {

    static const DevMem d TASTY_PERSIST(proc, mmio_dev_mem);
    return d;
}

PageSize page_size() {
    const long v = ::sysconf(_SC_PAGESIZE);
    return PageSize{v > 0 ? static_cast<std::size_t>(v) : std::size_t{4096}};
}

}

void* MapLength::map_shared(int prot, int fd, std::int64_t file_offset) const noexcept {
    return ::mmap(nullptr, v, prot, MAP_SHARED, fd, file_offset);
}

void MapLength::release(const volatile void* base) const noexcept {

    ::munmap(const_cast<void*>(base), v);
}

std::span<std::byte> MapLength::plain_bytes(void* base) const noexcept {
    return std::span<std::byte>(reinterpret_cast<std::byte*>(base), v);
}

MmioRegion::~MmioRegion() {
    if (base_ != nullptr)
        len_.release(base_);
}

MmioRegion::MmioRegion(MmioRegion&& o) noexcept
    : base_(std::exchange(o.base_, nullptr)),
      len_(std::exchange(o.len_, MapLength{})),
      access_(std::exchange(o.access_, Access::ReadWrite)) {}

MmioRegion& MmioRegion::operator=(MmioRegion&& o) noexcept {
    if (this != &o) {
        if (base_ != nullptr)
            len_.release(base_);
        base_ = std::exchange(o.base_, nullptr);
        len_ = std::exchange(o.len_, MapLength{});
        access_ = std::exchange(o.access_, Access::ReadWrite);
    }
    return *this;
}

Ex<MmioRegion> MmioRegion::map(PhysAddr phys, std::size_t len, Attr attr, Access access) {

    static_cast<void>(attr);

    const DevMem& d = dev_mem();
    if (d.fd < 0) {
        return std::unexpected(d.open_errno.refused(Errc::uio_open, ERR_SITE()));
    }
    return map_fd(d.fd, MapOffset::of(phys), MapLength{len}, access);
}

Ex<MmioRegion> MmioRegion::map_fd(int fd, MapOffset offset, MapLength len, Access access) {
    return map_at(fd, offset, len, access);
}

Ex<MmioRegion> MmioRegion::map_uio(int fd, UioMapIndex index, MapLength len, Access access) {
    return map_at(fd, index.offset_in(page_size().map_length()), len, access);
}

Ex<MmioRegion> MmioRegion::map_at(int fd, MapOffset offset, MapLength len, Access access) {
    if (len.empty()) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), 0});
    }

    if (!page_aligned(offset, page_size().map_length())) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), offset.low32()});
    }

    const int prot = access == Access::ReadOnly ? PROT_READ : (PROT_READ | PROT_WRITE);
    void* p = len.map_shared(prot, fd, offset.file_offset());
    if (p == MAP_FAILED) {
        return std::unexpected(infra::errno_error(Errc::mmap_failed, ERR_SITE(), errno));
    }

    MmioRegion r;
    r.base_ = p;

    r.len_ = len;
    r.access_ = access;
    return r;
}

}
