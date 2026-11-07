// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>

#include "infra/error.h"
#include "boards/cyclone_v/cyclone_v_handoff.h"
#include "hal/boot_handoff.h"
#include "hal/fpga_memory.h"
#include "hal/fpga_programmer.h"
#include "os/mmio_region.h"
#include "hal/phys_region.h"
#include "hal/register_window.h"
#include "hal/window_decl.h"
#include "os/types.h"
#include "infra/seat.h"

namespace mister::hal {
class BoardWindows;
}

namespace mister::boards::cyclone_v {

struct FpgaCtrl {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr FpgaCtrl with(FpgaCtrl bits) const noexcept {
        return FpgaCtrl{v | bits.v};
    }

    [[nodiscard]] constexpr FpgaCtrl without(FpgaCtrl bits) const noexcept {
        return FpgaCtrl{v & ~bits.v};
    }

    [[nodiscard]] constexpr bool has(FpgaCtrl bits) const noexcept {
        return (v & bits.v) == bits.v;
    }

    friend constexpr bool operator==(FpgaCtrl, FpgaCtrl) = default;
};

inline constexpr FpgaCtrl kCtrlNone{0u};
inline constexpr FpgaCtrl kCtrlCfgWdth{0x200u};
inline constexpr FpgaCtrl kCtrlAxiCfgEn{0x100u};
inline constexpr FpgaCtrl kCtrlNConfigPull{0x004u};
inline constexpr FpgaCtrl kCtrlNce{0x002u};
inline constexpr FpgaCtrl kCtrlEn{0x001u};
inline constexpr std::uint32_t kCtrlCdRatioLsb = 6;
inline constexpr FpgaCtrl kCtrlCdRatioMask{0x3u << kCtrlCdRatioLsb};

enum class FpgaMgrMode : std::uint32_t {
    Off = 0x0,
    Reset = 0x1,
    Cfg = 0x2,
    Init = 0x3,
    User = 0x4,
    Unknown = 0x5,
};

[[nodiscard]] constexpr std::uint32_t to_bits(FpgaMgrMode mode) noexcept {
    return static_cast<std::uint32_t>(mode);
}

enum class CdRatio : std::uint32_t { One = 0x0, Two = 0x1, Four = 0x2, Eight = 0x3 };

[[nodiscard]] constexpr std::uint32_t to_bits(CdRatio ratio) noexcept {
    return static_cast<std::uint32_t>(ratio);
}

[[nodiscard]] constexpr FpgaCtrl cd_ratio_field(CdRatio ratio) noexcept {
    return FpgaCtrl{to_bits(ratio) << kCtrlCdRatioLsb};
}

struct FpgaMgr {
    enum class Reg : std::uint32_t {
        Stat = 0x000,
        Ctrl = 0x004,
        DclkCnt = 0x008,
        DclkStat = 0x00C,
        GpioPortaEoi = 0x84C,
        GpioExtPorta = 0x850,
    };

    static constexpr std::size_t kSize = 0x1000;

    static constexpr std::uint32_t kStatModeMask = 0x7;
    static constexpr std::uint32_t kStatMselMask = 0xF8;
    static constexpr std::uint32_t kStatMselLsb = 3;

    static constexpr std::uint32_t kMselWideMask = 0x8;
    static constexpr std::uint32_t kMselRungMask = 0x3;

    static constexpr std::uint32_t kMonCrc = 0x8;
    static constexpr std::uint32_t kMonId = 0x4;
    static constexpr std::uint32_t kMonCd = 0x2;
    static constexpr std::uint32_t kMonNs = 0x1;

    static constexpr std::uint32_t kEoiAll = 0xFFF;

    static constexpr std::uint32_t kTimeoutCount = 0x1000000;
    static constexpr std::uint32_t kPollsPerPass = 0x100;

    static constexpr std::uint32_t kDclkEnterInit = 0x4;
    static constexpr std::uint32_t kDclkExitInit = 0x5000;
    static constexpr std::uint32_t kDclkStatDone = 0x1;
};

inline constexpr CdRatio kCdRatioFor[2][3] = {
    {CdRatio::One, CdRatio::Four, CdRatio::Eight},
    {CdRatio::One, CdRatio::Two, CdRatio::Four},
};

[[nodiscard]] constexpr std::optional<CdRatio> cd_ratio_of(std::uint32_t msel) noexcept {
    const std::uint32_t column = (msel & FpgaMgr::kMselWideMask) != 0u ? 0u : 1u;
    const std::uint32_t row = msel & FpgaMgr::kMselRungMask;
    if (row >= std::size(kCdRatioFor[0]))
        return std::nullopt;
    return kCdRatioFor[column][row];
}

struct FpgaMgrData {
    enum class Reg : std::uint32_t { Data = 0x00 };
    static constexpr std::size_t kSize = 0x1000;
    static constexpr std::size_t kBurstWords = 8;
};

inline constexpr os::PhysAddr kFpgaMgrBase{0xFF70'6000u};
inline constexpr os::PhysAddr kFpgaMgrDataBase{0xFFB9'0000u};

class Programmer final : public hal::IFpgaProgrammer {
    TASTY_SEAT_RESIDENT(Main);

public:
    [[nodiscard]] static Ex<Programmer> open(hal::BoardWindows& windows);

    [[nodiscard]] static Ex<Programmer> open_at(const char* manager_dir);

    static Programmer over(
        hal::RegisterWindow<FpgaMgr> mgr,
        hal::RegisterWindow<FpgaMgrData> data,
        hal::FpgaMemory handoff
    );

    Programmer(Programmer&& o) noexcept;
    Programmer& operator=(Programmer&&) = delete;

    [[nodiscard]] Ex<bool> program_begin() override;
    [[nodiscard]] Ex<void> program_chunk(std::span<const std::byte> chunk) override;
    [[nodiscard]] Ex<bool> program_end() override;
    [[nodiscard]] Ex<bool> program_step() override;
    [[nodiscard]] std::uint32_t pending_stage() noexcept override;

    bool programmed() const override;

    hal::IBootHandoff* boot_handoff() noexcept override {
        TASTY_SEAT_BODY(Programmer);
        return &page_;
    }

private:
    Programmer(
        hal::RegisterWindow<FpgaMgr> mgr,
        hal::RegisterWindow<FpgaMgrData> data,
        std::optional<hal::FpgaMemory> handoff
    );

    enum class StepPc : std::uint8_t {
        Idle,
        ResetWait,
        CfgWait,
        DclkEnterWait,
        InitWait,
        DclkExitWait,
        UserWait
    };
    void program_write(std::span<const std::byte> image);
    [[nodiscard]] Ex<void> poll_config_done();

    struct Awaited {
        bool hit;
        std::uint32_t left;
    };

    [[nodiscard]] Awaited await_(std::uint32_t polls);

    [[nodiscard]] bool rung_done_();
    [[nodiscard]] bool dclk_done_();
    void dclk_arm_(std::uint32_t count);
    void set_cd_ratio(CdRatio ratio);
    void ctrl_update(FpgaCtrl clear, FpgaCtrl set);
    FpgaMgrMode mode() const;

    std::uint32_t ext_nibble();

    std::optional<os::MmioRegion> mgr_pages_;
    std::optional<os::MmioRegion> data_pages_;
    std::optional<os::MmioRegion> handoff_pages_;

    hal::RegisterWindow<FpgaMgr> mgr_;
    hal::RegisterWindow<FpgaMgrData> data_;
    UbootHandoffPage page_;

    std::optional<hal::RegisterWindow<FpgaMgrData>::Transaction> data_txn_;
    bool tail_seen_ = false;
    StepPc step_ = StepPc::Idle;

    std::array<hal::WindowClaim, 2> claims_{};
};

static_assert(
    hal::IFpgaProgrammer::kProgramStepBudget * FpgaMgr::kPollsPerPass == FpgaMgr::kTimeoutCount,
    "program() must spend exactly stock's poll budget per wait"
);

}
