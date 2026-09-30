// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/axi.h"

#include "hal/dma_ring.h"
#include "hal/fpga_memory.h"
#include "os/uio_handle.h"

#include "infra/persist.h"

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

}  // namespace

Ex<MappedRegs> map_registers(os::PhysAddr phys, std::size_t len) {
    if (len == 0) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(phys)});
    }

    const std::size_t page = page_size();
    const std::size_t lead = static_cast<std::size_t>(phys.v) & (page - 1u);
    const os::PhysAddr aligned{phys.v - static_cast<std::uint32_t>(lead)};

    auto pages = os::MmioRegion::map(aligned, lead + len, os::MmioRegion::Attr::Device);
    if (!pages) return std::unexpected(pages.error());
    return MappedRegs{std::move(*pages), lead};
}

Ex<MappedRegs> map_lw_registers(PhysRegion lw, LwOffset off, std::size_t len) {

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

[[nodiscard]] Ex<MappedRegs> map_named(std::string_view uio_name, os::PhysAddr phys,
                                       std::size_t len) {
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
                auto pages = os::MmioRegion::map_fd(fd, os::PhysAddr{0}, len,
                                                    os::MmioRegion::Access::ReadWrite);
                ::close(fd);
                if (pages) return MappedRegs{std::move(*pages), 0, true};
            }
        }
    }

    auto via_dev_mem = map_registers(phys, len);
    if (!via_dev_mem) return std::unexpected(via_dev_mem.error());
    via_dev_mem->via_uio = false;
    return via_dev_mem;
}

std::uint32_t regs_read(const volatile void* base, std::size_t byte_off) {
    return *reinterpret_cast<const volatile std::uint32_t*>(
        reinterpret_cast<const volatile std::byte*>(base) + byte_off);
}

void regs_write(volatile void* base, std::size_t byte_off, std::uint32_t v) {
    *reinterpret_cast<volatile std::uint32_t*>(reinterpret_cast<volatile std::byte*>(base) +
                                               byte_off) = v;
}

void regs_fence() noexcept { std::atomic_thread_fence(std::memory_order_seq_cst); }

}  // namespace detail

namespace {

std::byte* bytes_of(const os::MmioRegion& m) noexcept {
    return reinterpret_cast<std::byte*>(const_cast<void*>(m.base()));
}

bool is_word_aligned(const std::byte* p) noexcept {
    return (reinterpret_cast<std::uintptr_t>(p) & 3u) == 0u;
}

bool is_power_of_two(std::size_t v) noexcept { return v != 0u && (v & (v - 1u)) == 0u; }

}  // namespace

Ex<FpgaMemory> FpgaMemory::map(PhysRegion r, os::MmioRegion::Access access) {
    if (r.len == 0) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(r.phys)});
    }

    auto pages = os::MmioRegion::map(r.phys, r.len, os::MmioRegion::Attr::UncachedNormal, access);
    if (!pages) return std::unexpected(pages.error());

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

Ex<UioMapping> map_uio_pages(int fd, os::PhysAddr node_base, PhysRegion r,
                             os::MmioRegion::Access access) {
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
    if (!pages) return std::unexpected(pages.error());
    return UioMapping{std::move(*pages), lead};
}

}  // namespace

Ex<FpgaMemory> FpgaMemory::map_uio(int fd, os::PhysAddr node_base, PhysRegion r,
                                   os::MmioRegion::Access access) {
    auto m = map_uio_pages(fd, node_base, r, access);
    if (!m) return std::unexpected(m.error());

    FpgaMemory mem;
    mem.region_ = r;
    mem.map_ = std::move(m->pages);
    mem.base_ = bytes_of(*mem.map_) + m->lead;
    mem.uio_bound_ = true;
    mem.access_ = access;
    return mem;
}

Ex<void> FpgaMemory::rebind_uio(int fd, os::PhysAddr node_base, PhysRegion r) {

    map_.reset();
    base_ = nullptr;
    region_ = PhysRegion{};
    uio_bound_ = false;
    ++generation_;

    auto m = map_uio_pages(fd, node_base, r, access_);
    if (!m) return std::unexpected(m.error());
    map_ = std::move(m->pages);
    base_ = bytes_of(*map_) + m->lead;
    region_ = r;
    uio_bound_ = true;
    return {};
}

FpgaMemory::FpgaMemory(FpgaMemory&& o) noexcept
    : region_(o.region_), map_(std::move(o.map_)), base_(o.base_), generation_(o.generation_),
      uio_bound_(o.uio_bound_), access_(o.access_) {
    o.region_ = PhysRegion{};
    o.base_ = nullptr;
    o.uio_bound_ = false;
    o.access_ = os::MmioRegion::Access::ReadWrite;
    ++o.generation_;
}

FpgaMemory& FpgaMemory::operator=(FpgaMemory&& o) noexcept {
    if (this != &o) {
        region_ = o.region_;
        map_ = std::move(o.map_);
        base_ = o.base_;
        uio_bound_ = o.uio_bound_;
        access_ = o.access_;

        ++generation_;
        o.region_ = PhysRegion{};
        o.base_ = nullptr;
        o.uio_bound_ = false;
        o.access_ = os::MmioRegion::Access::ReadWrite;
        ++o.generation_;
    }
    return *this;
}

FpgaMemory FpgaMemory::borrow(std::span<std::byte> backing, PhysRegion r) {
    if (backing.size() < r.len || !is_word_aligned(backing.data())) {
        fatal(Error{Errc::mmap_failed, ERR_SITE(), static_cast<std::uint32_t>(backing.size())},
              "FpgaMemory::borrow");
    }
    FpgaMemory mem;
    mem.region_ = r;
    mem.base_ = backing.data();
    return mem;
}

std::span<std::byte> FpgaMemory::view(std::size_t off, std::size_t len) {
    if (base_ == nullptr) {
        fatal(Error{Errc::mmap_failed, ERR_SITE(), os::low32(region_.phys)},
              "FpgaMemory::view unmapped");
    }
    if (off > region_.len || len > region_.len - off) {
        fatal(Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(off)},
              "FpgaMemory::view out of range");
    }
    return {base_ + off, len};
}

Ex<std::size_t> FpgaMemory::write_at(std::size_t off, std::span<const std::byte> src) {
    if (base_ == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(region_.phys)});
    }
    if (!span_fits(off, src.size(), region_.len)) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(off)});
    }
    if (!src.empty()) std::memcpy(base_ + off, src.data(), src.size());

    std::atomic_thread_fence(std::memory_order_release);
    return src.size();
}

Ex<std::size_t> FpgaMemory::read_at(std::size_t off, std::span<std::byte> dst) const {
    if (base_ == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(region_.phys)});
    }
    if (!span_fits(off, dst.size(), region_.len)) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(off)});
    }

    std::atomic_thread_fence(std::memory_order_acquire);
    if (!dst.empty()) std::memcpy(dst.data(), base_ + off, dst.size());
    return dst.size();
}

void FpgaMemory::publish() noexcept { std::atomic_thread_fence(std::memory_order_release); }

void FpgaMemory::handoff() noexcept {
#if defined(__arm__) || defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
}

Ex<void> FpgaMemory::rebind(PhysRegion r) {
    const bool was_uio = uio_bound_;
    map_.reset();
    base_ = nullptr;
    region_ = PhysRegion{};
    uio_bound_ = false;
    ++generation_;

    if (was_uio) {
        return std::unexpected(Error{Errc::unimplemented, ERR_SITE(), os::low32(r.phys)});
    }

    auto pages = os::MmioRegion::map(r.phys, r.len, os::MmioRegion::Attr::UncachedNormal, access_);
    if (!pages) return std::unexpected(pages.error());
    map_ = std::move(*pages);
    base_ = bytes_of(*map_);
    region_ = r;
    return {};
}

Ex<void> FpgaMemory::rebind(std::span<std::byte> backing, PhysRegion r) {

    map_.reset();
    base_ = nullptr;
    region_ = PhysRegion{};

    uio_bound_ = false;
    access_ = os::MmioRegion::Access::ReadWrite;
    ++generation_;

    if (backing.size() < r.len || !is_word_aligned(backing.data())) {
        return std::unexpected(
            Error{Errc::mmap_failed, ERR_SITE(), static_cast<std::uint32_t>(backing.size())});
    }
    base_ = backing.data();
    region_ = r;
    return {};
}

void FpgaMemory::store_release(std::size_t word_off, std::uint32_t v) {

    if (base_ == nullptr || region_.len < 4u || word_off > (region_.len - 4u) / 4u) {
        fatal(Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(word_off)},
              "FpgaMemory::store_release");
    }
    auto* p = reinterpret_cast<volatile std::uint32_t*>(base_ + word_off * 4u);

    std::atomic_thread_fence(std::memory_order_release);
    *p = v;
}

std::uint32_t FpgaMemory::load_acquire(std::size_t word_off) const {
    if (base_ == nullptr || region_.len < 4u || word_off > (region_.len - 4u) / 4u) {
        fatal(Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(word_off)},
              "FpgaMemory::load_acquire");
    }
    const auto* p = reinterpret_cast<const volatile std::uint32_t*>(base_ + word_off * 4u);
    const std::uint32_t v = *p;
    std::atomic_thread_fence(std::memory_order_acquire);
    return v;
}

Ex<DmaRing> DmaRing::create(FpgaMemory& mem, std::size_t off, std::size_t len) {
    if ((off & 3u) != 0u) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(off)});
    }
    if (len < kHeaderBytes + kMinCapacity) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(len)});
    }
    const std::size_t cap = len - kHeaderBytes;
    if (!is_power_of_two(cap)) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(cap)});
    }
    const PhysRegion& r = mem.region();
    if (off > r.len || len > r.len - off) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(off)});
    }

    DmaRing ring;
    ring.mem_ = &mem;
    ring.data_off_ = off + kHeaderBytes;
    ring.mask_ = cap - 1u;
    ring.head_word_ = off / 4u;
    ring.tail_word_ = off / 4u + 1u;
    ring.generation_ = mem.generation();

    mem.store_release(ring.head_word_, 0);
    mem.store_release(ring.tail_word_, 0);
    return ring;
}

void DmaRing::check_live() const {
    if (mem_ == nullptr || mem_->generation() != generation_) {
        fatal(Error{Errc::mmap_failed, ERR_SITE(), generation_},
              "DmaRing: region rebound under a live ring");
    }
}

std::size_t DmaRing::used() const {
    check_live();
    const std::size_t head = static_cast<std::size_t>(mem_->load_acquire(head_word_)) & mask_;
    const std::size_t tail = static_cast<std::size_t>(mem_->load_acquire(tail_word_)) & mask_;
    return (head - tail) & mask_;
}

std::size_t DmaRing::space() const { return mask_ - used(); }

std::span<std::byte> DmaRing::reserve(std::size_t n) {
    check_live();
    if (n == 0) return {};
    const std::size_t head = static_cast<std::size_t>(mem_->load_acquire(head_word_)) & mask_;
    const std::size_t tail = static_cast<std::size_t>(mem_->load_acquire(tail_word_)) & mask_;
    const std::size_t free_bytes = mask_ - ((head - tail) & mask_);
    const std::size_t to_end = capacity() - head;
    const std::size_t avail = free_bytes < to_end ? free_bytes : to_end;
    if (n > avail) return {};
    pending_ = n;
    return mem_->view(data_off_ + head, n);
}

void DmaRing::publish(std::size_t n) {
    check_live();
    if (n > pending_) {
        fatal(Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(n)},
              "DmaRing::publish beyond reserve");
    }
    pending_ = 0;
    const std::size_t head = static_cast<std::size_t>(mem_->load_acquire(head_word_)) & mask_;
    mem_->store_release(head_word_, static_cast<std::uint32_t>((head + n) & mask_));
}

std::span<const std::byte> DmaRing::peek() {
    check_live();
    const std::size_t head = static_cast<std::size_t>(mem_->load_acquire(head_word_)) & mask_;
    const std::size_t tail = static_cast<std::size_t>(mem_->load_acquire(tail_word_)) & mask_;
    const std::size_t ready = (head - tail) & mask_;
    const std::size_t to_end = capacity() - tail;
    const std::size_t n = ready < to_end ? ready : to_end;
    if (n == 0) return {};
    return mem_->view(data_off_ + tail, n);
}

void DmaRing::release(std::size_t n) {
    check_live();
    const std::size_t head = static_cast<std::size_t>(mem_->load_acquire(head_word_)) & mask_;
    const std::size_t tail = static_cast<std::size_t>(mem_->load_acquire(tail_word_)) & mask_;
    if (n > ((head - tail) & mask_)) {
        fatal(Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(n)},
              "DmaRing::release beyond peek");
    }
    mem_->store_release(tail_word_, static_cast<std::uint32_t>((tail + n) & mask_));
}

}  // namespace mister::hal
