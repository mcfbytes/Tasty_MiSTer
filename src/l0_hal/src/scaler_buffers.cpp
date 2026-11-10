// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/scaler_buffers.h"

#include "hal/fpga_memory.h"

#include "infra/error.h"

#include <cstdint>
#include <cstring>

namespace mister::hal {

Ex<ScalerBuffers> ScalerBuffers::map(const PhysRegion& scaler_out) {
    auto m = FpgaMemory::map_aperture(span_of(scaler_out), os::MmioRegion::Access::ReadOnly);
    if (!m)
        return std::unexpected(m.error());
    return ScalerBuffers{std::move(*m)};
}

ScalerBuffers ScalerBuffers::borrow(std::span<std::byte> backing) {
    return ScalerBuffers{
        FpgaMemory::borrow(backing, PhysRegion{os::PhysAddr{0}, backing.size(), "scaler-triple"})
    };
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
    if (dst.empty())
        return {};
    copy_wide(dst, mem_.view(off, dst.size()));
    return {};
}

void copy_wide(std::span<std::byte> dst, std::span<const std::byte> src) noexcept {
    if (src.size() > dst.size()) {
        fatal(Error{Errc::slot_range, ERR_SITE(), 0}, "hal::copy_wide past the destination span");
    }
    std::byte* out = dst.data();
    const std::byte* in = src.data();
    std::size_t n = src.size();
#if defined(__arm__) && defined(__ARM_FP)

    constexpr std::size_t kBlock = 128;
    constexpr std::uintptr_t kAlign = 8;
    const std::uintptr_t out_addr = reinterpret_cast<std::uintptr_t>(out);
    const std::uintptr_t in_addr = reinterpret_cast<std::uintptr_t>(in);
    if (((out_addr | in_addr) & (kAlign - 1)) == 0) {
        std::size_t blocks = n / kBlock;
        n -= blocks * kBlock;
        while (blocks-- != 0) {
            __asm__ volatile("vldmia %[s]!, {d0-d15}\n\t"
                             "vstmia %[d]!, {d0-d15}\n\t"
                             : [s] "+r"(in), [d] "+r"(out)
                             :
                             : "d0",
                               "d1",
                               "d2",
                               "d3",
                               "d4",
                               "d5",
                               "d6",
                               "d7",
                               "d8",
                               "d9",
                               "d10",
                               "d11",
                               "d12",
                               "d13",
                               "d14",
                               "d15",
                               "memory");
        }
    }
#endif
    std::memcpy(out, in, n);
}

}
