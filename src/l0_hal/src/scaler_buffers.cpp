// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/scaler_buffers.h"

#include "hal/fpga_memory.h"

#include <cstdint>
#include <cstring>

namespace mister::hal {

Ex<ScalerBuffers> ScalerBuffers::map(const PhysRegion& scaler_out) {
    auto m = FpgaMemory::map(span_of(scaler_out), os::MmioRegion::Access::ReadOnly);
    if (!m) return std::unexpected(m.error());
    return ScalerBuffers{std::move(*m)};
}

ScalerBuffers ScalerBuffers::borrow(std::span<std::byte> backing) {
    return ScalerBuffers{
        FpgaMemory::borrow(backing, PhysRegion{os::PhysAddr{0}, backing.size(), "scaler-triple"})};
}

Ex<ScalerHeader> ScalerBuffers::header(std::size_t off) const {
    std::byte raw[ScalerHeader::kBytes]{};
    if (auto r = mem_.read_at(off, std::span<std::byte>(raw)); !r)
        return std::unexpected(r.error());
    return ScalerHeader::decode(std::span<const std::byte, ScalerHeader::kBytes>(raw));
}

Ex<void> ScalerBuffers::copy(std::size_t off, std::span<std::byte> dst) {
    if (!span_fits(off, dst.size(), len()))
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    if (dst.empty()) return {};
    const std::span<std::byte> src = mem_.view(off, dst.size());
    copy_uncached(dst.data(), src.data(), dst.size());
    return {};
}

void copy_to_uncached(std::byte* dst, const std::byte* src, std::size_t n) noexcept {
#if defined(__arm__) && defined(__ARM_FP)

    const bool aligned =
        ((reinterpret_cast<std::uintptr_t>(dst) | reinterpret_cast<std::uintptr_t>(src)) & 7u) == 0;
    if (aligned) {
        std::size_t blocks = n / 128u;
        n -= blocks * 128u;
        while (blocks-- != 0) {
            __asm__ volatile("vldmia %[s]!, {d0-d15}\n\t"
                             "vstmia %[d]!, {d0-d15}\n\t"
                             : [s] "+r"(src), [d] "+r"(dst)
                             :
                             : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "d10",
                               "d11", "d12", "d13", "d14", "d15", "memory");
        }
    }
#endif
    std::memcpy(dst, src, n);
}

void copy_uncached(std::byte* dst, const std::byte* src, std::size_t n) noexcept {
    copy_to_uncached(dst, src, n);
}

}  // namespace mister::hal
