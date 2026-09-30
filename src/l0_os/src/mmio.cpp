// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/mmio_region.h"
#include "os/uio_handle.h"

#include "infra/persist.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>

namespace mister::os {
namespace {

constexpr int kDevMemFlags = O_RDWR | O_SYNC | O_CLOEXEC;

static_assert(sizeof(off_t) >= 8, "MmioRegion needs a 64-bit off_t");

struct DevMem {
    int fd = -1;
    std::uint32_t open_errno = 0;

    DevMem() {
        fd = ::open("/dev/mem", kDevMemFlags);
        if (fd < 0) open_errno = static_cast<std::uint32_t>(errno);
    }
};

const DevMem& dev_mem() {

    static const DevMem d TASTY_PERSIST(proc, mmio_dev_mem);
    return d;
}

std::size_t page_size() {
    const long v = ::sysconf(_SC_PAGESIZE);
    return v > 0 ? static_cast<std::size_t>(v) : std::size_t{4096};
}

constexpr unsigned kMaxUioNodes = 64;

constexpr char kUioClassRoot[] = "/sys/class/uio";

bool read_uio_name(const char* root, unsigned idx, char* out, std::size_t cap) {
    char path[128];
    const int n = std::snprintf(path, sizeof path, "%s/uio%u/name", root, idx);
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof path) return false;

    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    const ssize_t got = ::read(fd, out, cap - 1);
    ::close(fd);
    if (got <= 0) return false;

    std::size_t len = static_cast<std::size_t>(got);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r'))
        --len;
    out[len] = '\0';
    return true;
}

}  // namespace

int find_uio_node_under(const char* sysfs_root, std::string_view name) noexcept {
    if (name.empty() || name.size() >= 48) return -1;
    char want[48];
    std::memcpy(want, name.data(), name.size());
    want[name.size()] = '\0';

    for (unsigned i = 0; i < kMaxUioNodes; ++i) {
        char have[48];
        if (!read_uio_name(sysfs_root, i, have, sizeof have)) continue;
        if (std::strcmp(have, want) == 0) return static_cast<int>(i);
    }
    return -1;
}

int find_uio_node(std::string_view name) noexcept {
    return find_uio_node_under(kUioClassRoot, name);
}

namespace {

int find_doorbell_uio_node(std::string_view prefix, unsigned line) {
    char want[48];
    const int n = std::snprintf(want, sizeof want, "%.*s%u", static_cast<int>(prefix.size()),
                                prefix.data(), line + 1);
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof want) return -1;
    return find_uio_node_under(kUioClassRoot, want);
}

}  // namespace

MmioRegion::~MmioRegion() {
    if (base_ != nullptr) {

        ::munmap(const_cast<void*>(base_), len_);
    }
}

MmioRegion::MmioRegion(MmioRegion&& o) noexcept
    : base_(std::exchange(o.base_, nullptr)), len_(std::exchange(o.len_, 0)),
      access_(std::exchange(o.access_, Access::ReadWrite)) {}

MmioRegion& MmioRegion::operator=(MmioRegion&& o) noexcept {
    if (this != &o) {
        if (base_ != nullptr) ::munmap(const_cast<void*>(base_), len_);
        base_ = std::exchange(o.base_, nullptr);
        len_ = std::exchange(o.len_, 0);
        access_ = std::exchange(o.access_, Access::ReadWrite);
    }
    return *this;
}

Ex<MmioRegion> MmioRegion::map(PhysAddr phys, std::size_t len, Attr attr, Access access) {

    static_cast<void>(attr);

    const DevMem& d = dev_mem();
    if (d.fd < 0) {
        return std::unexpected(Error{Errc::uio_open, ERR_SITE(), d.open_errno});
    }
    return map_fd(d.fd, phys, len, access);
}

Ex<MmioRegion> MmioRegion::map_fd(int fd, PhysAddr offset, std::size_t len, Access access) {
    if (len == 0) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), 0});
    }

    if ((static_cast<std::size_t>(offset.v) & (page_size() - 1)) != 0) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), low32(offset)});
    }

    const int prot = access == Access::ReadOnly ? PROT_READ : (PROT_READ | PROT_WRITE);
    void* p = ::mmap(nullptr, len, prot, MAP_SHARED, fd, static_cast<off_t>(offset.v));
    if (p == MAP_FAILED) {
        return std::unexpected(
            Error{Errc::mmap_failed, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }

    MmioRegion r;
    r.base_ = p;
    r.len_ = len;

    r.access_ = access;
    return r;
}

Ex<UioHandle> UioHandle::open(UioLine line, const UioLineSpace& space) {
    const unsigned n = std::to_underlying(line);
    if (n >= space.lines || space.node_prefix.empty()) {
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), n});
    }

    const int idx = find_doorbell_uio_node(space.node_prefix, n);
    if (idx < 0) {

        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), n});
    }

    char path[32];
    const int w = std::snprintf(path, sizeof path, "/dev/uio%d", idx);
    if (w <= 0 || static_cast<std::size_t>(w) >= sizeof path) {
        return std::unexpected(Error{Errc::uio_open, ERR_SITE(), n});
    }

    const int fd = ::open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        return std::unexpected(
            Error{Errc::uio_open, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    return adopt(line, UniqueFd{fd});
}

Ex<UioHandle> UioHandle::adopt(UioLine line, UniqueFd fd) {
    if (!fd.valid()) {
        return std::unexpected(Error{Errc::uio_open, ERR_SITE(), std::to_underlying(line)});
    }

    const std::uint32_t enable = 1;
    const ssize_t n = ::write(fd.get(), &enable, sizeof enable);
    if (n < 0) {
        return std::unexpected(
            Error{Errc::uio_open, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    if (n != static_cast<ssize_t>(sizeof enable)) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(n)});
    }

    UioHandle h;
    h.fd_ = std::move(fd);
    h.line_ = line;
    return h;
}

Ex<std::uint32_t> UioHandle::consume() {

    std::uint32_t count = 0;
    const ssize_t n = ::read(fd_.get(), &count, sizeof count);
    if (n < 0) {
        const int e = errno;

        return std::unexpected(Error{e == EAGAIN ? Errc::would_block : Errc::io, ERR_SITE(),
                                     static_cast<std::uint32_t>(e)});
    }
    if (n != static_cast<ssize_t>(sizeof count)) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(n)});
    }
    return count;
}

Ex<void> UioHandle::rearm() {
    const std::uint32_t enable = 1;
    const ssize_t w = ::write(fd_.get(), &enable, sizeof enable);
    if (w < 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    if (w != static_cast<ssize_t>(sizeof enable)) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(w)});
    }
    return {};
}

}  // namespace mister::os
