// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/mmio_region.h"
#include "os/path_buf.h"
#include "os/uio_handle.h"

#include "infra/persist.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>

namespace mister::os {
namespace {

constexpr int kDevMemFlags = O_RDWR | O_SYNC | O_CLOEXEC;

static_assert(sizeof(off_t) >= 8, "MmioRegion needs a 64-bit off_t");

struct DevMem {
    int fd = -1;
    int open_errno = 0;

    DevMem() {
        fd = ::open("/dev/mem", kDevMemFlags);
        if (fd < 0)
            open_errno = errno;
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

[[nodiscard]] Ex<void> read_uio_name(const char* root, unsigned idx, std::span<char> out) {
    if (out.empty())
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    char path[kUioSysfsPathBuf];
    if (!format_path(std::span<char>(path), "%s/uio%u/name", root, idx)) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(sizeof path)}
        );
    }

    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    const ssize_t got = ::read(fd, out.data(), out.size() - 1);
    ::close(fd);
    if (got <= 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(got)});
    }

    std::size_t len = static_cast<std::size_t>(got);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r'))
        --len;
    out[len] = '\0';
    return {};
}

}

int find_uio_node_under(const char* sysfs_root, std::string_view name) noexcept {
    char want[kUioNodeNameBuf];
    if (name.empty() || name.size() >= sizeof want)
        return -1;
    std::memcpy(want, name.data(), name.size());
    want[name.size()] = '\0';

    for (unsigned i = 0; i < kMaxUioNodes; ++i) {
        char have[kUioNodeNameBuf];
        if (!read_uio_name(sysfs_root, i, std::span<char>(have)))
            continue;
        if (std::string_view{have} == want)
            return static_cast<int>(i);
    }
    return -1;
}

int find_uio_node(std::string_view name) noexcept {
    return find_uio_node_under(kUioClassRoot, name);
}

namespace {

int find_doorbell_uio_node(const UioLineSpace& space, UioLine line) {
    char want[kUioNodeNameBuf];
    if (!format_path(
            std::span<char>(want),
            "%.*s%u",
            static_cast<int>(space.node_prefix.size()),
            space.node_prefix.data(),
            to_ordinal(line) + 1
        )) {
        return -1;
    }
    char root[kUioSysfsPathBuf];
    if (!format_path(
            std::span<char>(root),
            "%.*s",
            static_cast<int>(space.sysfs_root.size()),
            space.sysfs_root.data()
        )) {
        return -1;
    }
    return find_uio_node_under(root, want);
}

}

MmioRegion::~MmioRegion() {
    if (base_ != nullptr) {

        ::munmap(const_cast<void*>(base_), len_);
    }
}

MmioRegion::MmioRegion(MmioRegion&& o) noexcept
    : base_(std::exchange(o.base_, nullptr)),
      len_(std::exchange(o.len_, 0)),
      access_(std::exchange(o.access_, Access::ReadWrite)) {}

MmioRegion& MmioRegion::operator=(MmioRegion&& o) noexcept {
    if (this != &o) {
        if (base_ != nullptr)
            ::munmap(const_cast<void*>(base_), len_);
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
        return std::unexpected(infra::errno_error(Errc::uio_open, ERR_SITE(), d.open_errno));
    }
    return map_fd(d.fd, phys, len, access);
}

Ex<MmioRegion> MmioRegion::map_fd(int fd, PhysAddr offset, std::size_t len, Access access) {
    if (len == 0) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), 0});
    }

    if (offset.misaligned_to(page_size())) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), low32(offset)});
    }

    const int prot = access == Access::ReadOnly ? PROT_READ : (PROT_READ | PROT_WRITE);
    void* p = ::mmap(nullptr, len, prot, MAP_SHARED, fd, offset.file_offset());
    if (p == MAP_FAILED) {
        return std::unexpected(infra::errno_error(Errc::mmap_failed, ERR_SITE(), errno));
    }

    MmioRegion r;
    r.base_ = p;
    r.len_ = len;

    r.access_ = access;
    return r;
}

Ex<UioHandle> UioHandle::open(UioLine line, const UioLineSpace& space) {
    const std::uint32_t n = to_ordinal(line);
    if (!space.contains(line) || space.node_prefix.empty()) {
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), n});
    }

    const int idx = find_doorbell_uio_node(space, line);
    if (idx < 0) {

        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), n});
    }

    char path[kUioDevPathBuf];
    if (!format_path(
            std::span<char>(path),
            "%.*s/uio%d",
            static_cast<int>(space.dev_dir.size()),
            space.dev_dir.data(),
            idx
        )) {
        return std::unexpected(Error{Errc::uio_open, ERR_SITE(), n});
    }

    const int fd = ::open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        return std::unexpected(infra::errno_error(Errc::uio_open, ERR_SITE(), errno));
    }
    return adopt(line, UniqueFd{fd});
}

Ex<UioHandle> UioHandle::adopt(UioLine line, UniqueFd fd) {
    if (!fd.valid()) {
        return std::unexpected(Error{Errc::uio_open, ERR_SITE(), to_ordinal(line)});
    }

    const int flags = ::fcntl(fd.get(), F_GETFL);
    if (flags < 0 || (flags & O_NONBLOCK) == 0) {
        return std::unexpected(Error{Errc::uio_open, ERR_SITE(), to_ordinal(line)});
    }
    UioHandle h;
    h.fd_ = std::move(fd);
    h.line_ = line;
    return h;
}

Ex<std::uint32_t> UioLineRef::consume() const {

    std::uint32_t count = 0;
    const ssize_t n = ::read(fd_, &count, sizeof count);
    if (n < 0) {
        const int e = errno;

        return std::unexpected(
            infra::errno_error(e == EAGAIN ? Errc::would_block : Errc::io, ERR_SITE(), e)
        );
    }
    if (n != static_cast<ssize_t>(sizeof count)) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(n)});
    }
    return count;
}

Ex<void> UioLineRef::rearm() const {
    const std::uint32_t enable = 1;
    const ssize_t w = ::write(fd_, &enable, sizeof enable);
    if (w < 0) {
        return std::unexpected(infra::errno_error(Errc::io, ERR_SITE(), errno));
    }
    if (w != static_cast<ssize_t>(sizeof enable)) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(w)});
    }
    return {};
}

}
