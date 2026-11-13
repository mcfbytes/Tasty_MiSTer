// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/st_sysctrl.h"
#include "cores/types.h"
#include "infra/endian.h"
#include "proto/status_bit.h"
#include "proto/status_word.h"
#include "proto/types.h"
#include "cores/st_slot.h"

namespace mister::cores::st_ctrl {

struct CtrlWord {

    struct Bit {
        std::uint32_t v = 0;
        friend constexpr bool operator==(Bit, Bit) = default;
    };

    struct MemConfig {
        std::uint8_t v = 0;
        friend constexpr bool operator==(MemConfig, MemConfig) = default;
        friend constexpr auto operator<=>(MemConfig, MemConfig) = default;
    };

    struct Chipset {
        std::uint8_t v = 0;
        friend constexpr bool operator==(Chipset, Chipset) = default;
    };

    struct Scanlines {
        std::uint8_t v = 0;
        friend constexpr bool operator==(Scanlines, Scanlines) = default;
    };

    struct Aspect {
        std::uint8_t v = 0;
        friend constexpr bool operator==(Aspect, Aspect) = default;
    };

    std::uint32_t v = 0;

    inline static constexpr Bit kCpuReset{::mister::cores::st_ctrl::kCpuReset};
    inline static constexpr Bit kFdcWriteProtectA{::mister::cores::st_ctrl::kFdcWriteProtectA};
    inline static constexpr Bit kFdcWriteProtectB{::mister::cores::st_ctrl::kFdcWriteProtectB};
    inline static constexpr Bit kVideoColor{::mister::cores::st_ctrl::kVideoColor};
    inline static constexpr Bit kVideoAspect1{::mister::cores::st_ctrl::kVideoAspect1};
    inline static constexpr Bit kVideoAspect2{::mister::cores::st_ctrl::kVideoAspect2};
    inline static constexpr Bit kAcsi0Enable{::mister::cores::st_ctrl::kAcsi0Enable};

    inline static constexpr Bit kAcsi1Enable{::mister::cores::st_ctrl::kAcsi0Enable << 1u};
    inline static constexpr Bit kTurbo{::mister::cores::st_ctrl::kTurbo};
    inline static constexpr Bit kBlitter{::mister::cores::st_ctrl::kBlitter};
    inline static constexpr Bit kStereo{::mister::cores::st_ctrl::kStereo};
    inline static constexpr Bit kSte{::mister::cores::st_ctrl::kSte};
    inline static constexpr Bit kDongle{::mister::cores::st_ctrl::kDongle};
    inline static constexpr Bit kViking{::mister::cores::st_ctrl::kViking};
    inline static constexpr Bit kBorder{::mister::cores::st_ctrl::kBorder};
    inline static constexpr Bit kMde60{::mister::cores::st_ctrl::kMde60};

    inline static constexpr MemConfig kMem512{0};
    inline static constexpr MemConfig kMem1M{1};
    inline static constexpr MemConfig kMem2M{2};
    inline static constexpr MemConfig kMem4M{3};
    inline static constexpr MemConfig kMem8M{4};
    inline static constexpr MemConfig kMem14M{5};

    inline static constexpr Chipset kChipsetSt{0};
    inline static constexpr Chipset kChipsetSte{1};
    inline static constexpr Chipset kChipsetMegaSte{2};
    inline static constexpr Chipset kChipsetStPlus{3};

    inline static constexpr Scanlines kScanlinesOff{0};
    inline static constexpr Scanlines kScanlinesOne{1};
    inline static constexpr Scanlines kScanlinesTwo{2};
    inline static constexpr Scanlines kScanlinesThree{3};

    inline static constexpr Aspect kAspect0{0};
    inline static constexpr Aspect kAspect1{1};
    inline static constexpr Aspect kAspect2{2};
    inline static constexpr Aspect kAspect3{3};

    inline static constexpr unsigned kMemConfigShift = 1;
    inline static constexpr std::uint32_t kMemConfigField = 0x7u;
    inline static constexpr unsigned kScanlinesShift = 20;
    inline static constexpr unsigned kChipsetShift = 23;

    inline static constexpr std::uint32_t kChoiceField = 0x3u;

    inline static constexpr std::size_t kMemConfigCount = kMemConfigField + 1u;
    inline static constexpr std::size_t kChoiceCount = kChoiceField + 1u;

    constexpr void store_le(std::span<std::byte, 4> dst) const noexcept {
        infra::store_le<std::uint32_t>(dst, v);
    }

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return v; }

    constexpr void store_raw(std::span<std::uint32_t, 1> dst) const noexcept { dst[0] = raw(); }

    [[nodiscard]] constexpr proto::StatusWord status_word() const noexcept {
        proto::StatusWord w{};
        w.words[0] = static_cast<std::uint16_t>(v);
        w.words[1] = static_cast<std::uint16_t>(v >> proto::StatusBit::kWordBits);
        return w;
    }

    [[nodiscard]] static constexpr CtrlWord of(const proto::StatusWord& w) noexcept {
        return CtrlWord{
            static_cast<std::uint32_t>(w.words[0])
            | (static_cast<std::uint32_t>(w.words[1]) << proto::StatusBit::kWordBits)
        };
    }

    [[nodiscard]] constexpr bool has(Bit b) const noexcept { return (v & b.v) != 0; }

    [[nodiscard]] constexpr CtrlWord with(Bit b) const noexcept { return CtrlWord{v | b.v}; }

    [[nodiscard]] constexpr CtrlWord without(Bit b) const noexcept { return CtrlWord{v & ~b.v}; }

    [[nodiscard]] constexpr MemConfig mem_config() const noexcept {
        return MemConfig{static_cast<std::uint8_t>((v >> kMemConfigShift) & kMemConfigField)};
    }

    [[nodiscard]] constexpr Chipset chipset() const noexcept {
        return Chipset{static_cast<std::uint8_t>((v >> kChipsetShift) & kChoiceField)};
    }

    [[nodiscard]] constexpr Scanlines scanlines() const noexcept {
        return Scanlines{static_cast<std::uint8_t>((v >> kScanlinesShift) & kChoiceField)};
    }

    [[nodiscard]] constexpr Aspect aspect() const noexcept {
        return Aspect{static_cast<std::uint8_t>(
            (has(kVideoAspect1) ? kAspect1.v : kAspect0.v)
            | (has(kVideoAspect2) ? kAspect2.v : kAspect0.v)
        )};
    }

    friend constexpr bool operator==(CtrlWord, CtrlWord) = default;
};

[[nodiscard]] constexpr CtrlWord::Bit write_protect_bit(StSlot slot) noexcept {
    return slot == StSlot::FloppyA ? CtrlWord::kFdcWriteProtectA : CtrlWord::kFdcWriteProtectB;
}

[[nodiscard]] constexpr CtrlWord::Bit acsi_enable_bit(AcsiTarget target) noexcept {
    return target == AcsiTarget{0} ? CtrlWord::kAcsi0Enable : CtrlWord::kAcsi1Enable;
}

}
