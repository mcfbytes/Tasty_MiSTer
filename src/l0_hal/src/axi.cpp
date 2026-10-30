// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/axi.h"

#include "hal/fpga_memory.h"
#include "os/small_file.h"
#include "os/uio_handle.h"

#include "infra/parse_int.h"
#include "infra/persist.h"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string_view>

namespace mister::hal {

namespace detail {
namespace {

std::size_t page_size() noexcept {

    static const std::size_t sz TASTY_PERSIST(proc, axi_page_size) = [] {
        const long v = ::sysconf(_SC_PAGESIZE);
        return v > 0 ? static_cast<std::size_t>(v) : std::size_t{4096};
    }();
    return sz;
}

}

[[nodiscard]] Ex<MappedRegs> map_registers(os::PhysAddr phys, std::size_t len) {
    if (len == 0) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(phys)});
    }

    const std::size_t page = page_size();
    const std::size_t lead = static_cast<std::size_t>(phys.v) & (page - 1u);
    const os::PhysAddr aligned{phys.v - static_cast<std::uint32_t>(lead)};

    auto pages = os::MmioRegion::map(aligned, lead + len, os::MmioRegion::Attr::Device);
    if (!pages)
        return std::unexpected(pages.error());
    return MappedRegs{std::move(*pages), lead};
}

[[nodiscard]] Ex<MappedRegs> map_lw_registers(PhysRegion lw, LwOffset off, std::size_t len) {

    if (lw.len == 0) {
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), off.v});
    }

    if (len == 0 || len > lw.len || static_cast<std::size_t>(off.v) > lw.len - len) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), off.v});
    }
    if ((off.v & 3u) != 0u) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), off.v});
    }
    return map_registers(os::PhysAddr{lw.phys.v + off.v}, len);
}

[[nodiscard]] Ex<MappedRegs> map_named(
    std::string_view uio_name,
    os::PhysAddr phys,
    std::size_t len
) {
    if (len == 0) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(phys)});
    }

    const int idx = os::find_uio_node(uio_name);
    if (idx >= 0) {
        char path[32];
        const int w = std::snprintf(path, sizeof path, "/dev/uio%d", idx);
        if (w > 0 && static_cast<std::size_t>(w) < sizeof path) {
            const int fd = ::open(path, O_RDWR | O_CLOEXEC);
            if (fd >= 0) {
                auto pages = os::MmioRegion::map_fd(
                    fd,
                    os::PhysAddr{0},
                    len,
                    os::MmioRegion::Access::ReadWrite
                );
                ::close(fd);
                if (pages)
                    return MappedRegs{std::move(*pages), 0, true};
            }
        }
    }

    auto via_dev_mem = map_registers(phys, len);
    if (!via_dev_mem)
        return std::unexpected(via_dev_mem.error());
    via_dev_mem->via_uio = false;
    return via_dev_mem;
}

std::uint32_t regs_read(const volatile void* base, std::size_t byte_off) {
    return *reinterpret_cast<const volatile std::uint32_t*>(
        reinterpret_cast<const volatile std::byte*>(base) + byte_off
    );
}

void regs_write(volatile void* base, std::size_t byte_off, std::uint32_t v) {
    *reinterpret_cast<volatile std::uint32_t*>(
        reinterpret_cast<volatile std::byte*>(base) + byte_off
    ) = v;
}

void regs_fence() noexcept {
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

}

namespace {

std::byte* bytes_of(const os::MmioRegion& m) noexcept {
    return reinterpret_cast<std::byte*>(const_cast<void*>(m.base()));
}

bool is_word_aligned(const std::byte* p) noexcept {
    return (reinterpret_cast<std::uintptr_t>(p) & 3u) == 0u;
}

}

namespace {

template <class T>
std::optional<T> uio_map_attr(const char* root, int idx, const char* leaf) noexcept {
    char path[160];
    const int n = std::snprintf(path, sizeof path, "%s/uio%d/maps/map0/%s", root, idx, leaf);
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof path)
        return std::nullopt;
    std::array<char, 32> buf{};
    const auto got = os::read_small_file(path, std::span<char>(buf));
    if (!got || *got == 0 || *got >= buf.size())
        return std::nullopt;
    std::string_view text = infra::trim_line_end(std::string_view(buf.data(), *got));

    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
    }
    return infra::parse_int<T>(text, 16);
}

}

ApertureChoice resolve_aperture(const char* sysfs_root, const PhysRegion& r) noexcept {
    ApertureChoice c;
    if (r.len == 0)
        return c;

    const int idx = os::find_uio_node_under(sysfs_root, kApertureUioName);
    if (idx < 0)
        return c;

    const auto addr = uio_map_attr<std::uint32_t>(sysfs_root, idx, "addr");
    const auto size = uio_map_attr<std::uint32_t>(sysfs_root, idx, "size");
    if (!addr || !size)
        return c;

    if (os::PhysAddr{*addr} != kFpgaMemBase)
        return c;
    const std::uint64_t off = r.phys.v - kFpgaMemBase.v;
    if (off > *size || r.len > (*size - off))
        return c;

    c.via_uio = true;
    c.node = idx;
    c.node_base = os::PhysAddr{*addr};
    c.node_size = *size;
    return c;
}

Ex<FpgaMemory> FpgaMemory::map_aperture(PhysRegion r, os::MmioRegion::Access access) {
    const ApertureChoice choice = resolve_aperture(os::kUioClassRoot, r);
    if (choice.via_uio) {
        char path[32];
        const int w = std::snprintf(path, sizeof path, "/dev/uio%d", choice.node);
        if (w > 0 && static_cast<std::size_t>(w) < sizeof path) {
            const int fd = ::open(path, O_RDWR | O_CLOEXEC);
            if (fd >= 0) {
                auto mem = map_uio(fd, choice.node_base, r, access);
                ::close(fd);
                if (mem)
                    return mem;
            }
        }
    }

    return map(r, access);
}

Ex<FpgaMemory> FpgaMemory::map(PhysRegion r, os::MmioRegion::Access access) {
    if (r.len == 0) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(r.phys)});
    }

    auto pages = os::MmioRegion::map(r.phys, r.len, os::MmioRegion::Attr::UncachedNormal, access);
    if (!pages)
        return std::unexpected(pages.error());

    FpgaMemory mem;
    mem.region_ = r;
    mem.map_ = std::move(*pages);
    mem.base_ = bytes_of(*mem.map_);
    mem.access_ = access;
    return mem;
}

namespace {

struct UioMapping {
    os::MmioRegion pages;
    std::size_t lead = 0;
};

[[nodiscard]] Ex<UioMapping> map_uio_pages(
    int fd,
    os::PhysAddr node_base,
    PhysRegion r,
    os::MmioRegion::Access access
) {
    if (r.len == 0) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(r.phys)});
    }
    if (r.phys.v < node_base.v) {

        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(r.phys)});
    }

    const std::uint64_t gap = r.phys.v - node_base.v;
    if (gap > std::uint64_t{static_cast<std::size_t>(-1) - r.len}) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(r.phys)});
    }
    const auto lead = static_cast<std::size_t>(gap);

    auto pages = os::MmioRegion::map_fd(fd, os::PhysAddr{0}, lead + r.len, access);
    if (!pages)
        return std::unexpected(pages.error());
    return UioMapping{std::move(*pages), lead};
}

}

Ex<FpgaMemory> FpgaMemory::map_uio(
    int fd,
    os::PhysAddr node_base,
    PhysRegion r,
    os::MmioRegion::Access access
) {
    auto m = map_uio_pages(fd, node_base, r, access);
    if (!m)
        return std::unexpected(m.error());

    FpgaMemory mem;
    mem.region_ = r;
    mem.map_ = std::move(m->pages);
    mem.base_ = bytes_of(*mem.map_) + m->lead;
    mem.uio_bound_ = true;
    mem.access_ = access;
    return mem;
}

FpgaMemory::FpgaMemory(FpgaMemory&& o) noexcept
    : region_(o.region_),
      map_(std::move(o.map_)),
      base_(o.base_),
      uio_bound_(o.uio_bound_),
      access_(o.access_) {
    o.region_ = PhysRegion{};
    o.base_ = nullptr;
    o.uio_bound_ = false;
    o.access_ = os::MmioRegion::Access::ReadWrite;
}

FpgaMemory& FpgaMemory::operator=(FpgaMemory&& o) noexcept {
    if (this != &o) {
        region_ = o.region_;
        map_ = std::move(o.map_);
        base_ = o.base_;
        uio_bound_ = o.uio_bound_;
        access_ = o.access_;
        o.region_ = PhysRegion{};
        o.base_ = nullptr;
        o.uio_bound_ = false;
        o.access_ = os::MmioRegion::Access::ReadWrite;
    }
    return *this;
}

FpgaMemory FpgaMemory::borrow(std::span<std::byte> backing, PhysRegion r) {
    if (backing.size() < r.len || !is_word_aligned(backing.data())) {
        fatal(
            Error{Errc::mmap_failed, ERR_SITE(), static_cast<std::uint32_t>(backing.size())},
            "FpgaMemory::borrow"
        );
    }
    FpgaMemory mem;
    mem.region_ = r;
    mem.base_ = backing.data();
    return mem;
}

std::span<std::byte> FpgaMemory::view(std::size_t off, std::size_t len) {
    if (base_ == nullptr) {
        fatal(
            Error{Errc::mmap_failed, ERR_SITE(), os::low32(region_.phys)},
            "FpgaMemory::view unmapped"
        );
    }
    if (off > region_.len || len > region_.len - off) {
        fatal(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(off)},
            "FpgaMemory::view out of range"
        );
    }
    return {base_ + off, len};
}

Ex<std::size_t> FpgaMemory::write_at(std::size_t off, std::span<const std::byte> src) {
    if (base_ == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(region_.phys)});
    }
    if (!span_fits(off, src.size(), region_.len)) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(off)}
        );
    }
    if (!src.empty())
        std::memcpy(base_ + off, src.data(), src.size());

    std::atomic_thread_fence(std::memory_order_release);
    return src.size();
}

Ex<std::size_t> FpgaMemory::read_at(std::size_t off, std::span<std::byte> dst) const {
    if (base_ == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(region_.phys)});
    }
    if (!span_fits(off, dst.size(), region_.len)) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(off)}
        );
    }

    std::atomic_thread_fence(std::memory_order_acquire);
    if (!dst.empty())
        std::memcpy(dst.data(), base_ + off, dst.size());
    return dst.size();
}

void FpgaMemory::publish() noexcept {
    std::atomic_thread_fence(std::memory_order_release);
}

void FpgaMemory::handoff() noexcept {
#if defined(__arm__) || defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
}

void FpgaMemory::store_release(std::size_t word_off, std::uint32_t v) {

    if (base_ == nullptr || region_.len < 4u || word_off > (region_.len - 4u) / 4u) {
        fatal(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(word_off)},
            "FpgaMemory::store_release"
        );
    }
    auto* p = reinterpret_cast<volatile std::uint32_t*>(base_ + word_off * 4u);

    std::atomic_thread_fence(std::memory_order_release);
    *p = v;
}

std::uint32_t FpgaMemory::load_acquire(std::size_t word_off) const {
    if (base_ == nullptr || region_.len < 4u || word_off > (region_.len - 4u) / 4u) {
        fatal(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(word_off)},
            "FpgaMemory::load_acquire"
        );
    }
    const auto* p = reinterpret_cast<const volatile std::uint32_t*>(base_ + word_off * 4u);
    const std::uint32_t v = *p;
    std::atomic_thread_fence(std::memory_order_acquire);
    return v;
}

}
