// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "infra/error.h"
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

namespace mister::boards {

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

    static constexpr std::uint32_t kCtrlCfgWdth = 0x200;
    static constexpr std::uint32_t kCtrlAxiCfgEn = 0x100;
    static constexpr std::uint32_t kCtrlNConfigPull = 0x004;
    static constexpr std::uint32_t kCtrlNce = 0x002;
    static constexpr std::uint32_t kCtrlEn = 0x001;
    static constexpr std::uint32_t kCtrlCdRatioLsb = 6;
    static constexpr std::uint32_t kCtrlCdRatioMask = 0x3u << kCtrlCdRatioLsb;

    static constexpr std::uint32_t kMonCrc = 0x8;
    static constexpr std::uint32_t kMonId = 0x4;
    static constexpr std::uint32_t kMonCd = 0x2;
    static constexpr std::uint32_t kMonNs = 0x1;

    static constexpr std::uint32_t kEoiAll = 0xFFF;

    static constexpr std::uint32_t kModeFpgaOff = 0x0;
    static constexpr std::uint32_t kModeResetPhase = 0x1;
    static constexpr std::uint32_t kModeCfgPhase = 0x2;
    static constexpr std::uint32_t kModeInitPhase = 0x3;
    static constexpr std::uint32_t kModeUserMode = 0x4;
    static constexpr std::uint32_t kModeUnknown = 0x5;

    static constexpr std::uint32_t kCdRatio1 = 0x0;
    static constexpr std::uint32_t kCdRatio2 = 0x1;
    static constexpr std::uint32_t kCdRatio4 = 0x2;
    static constexpr std::uint32_t kCdRatio8 = 0x3;

    static constexpr std::uint32_t kTimeoutCount = 0x1000000;
    static constexpr std::uint32_t kPollsPerPass = 0x100;

    static constexpr std::uint32_t kDclkEnterInit = 0x4;
    static constexpr std::uint32_t kDclkExitInit = 0x5000;
    static constexpr std::uint32_t kDclkStatDone = 0x1;
};

struct FpgaMgrData {
    enum class Reg : std::uint32_t { Data = 0x00 };
    static constexpr std::size_t kSize = 0x1000;
    static constexpr std::size_t kBurstWords = 8;
};

inline constexpr os::PhysAddr kFpgaMgrBase{0xFF70'6000u};
inline constexpr os::PhysAddr kFpgaMgrDataBase{0xFFB9'0000u};

inline constexpr hal::PhysRegion kHandoffPage{os::PhysAddr{0x1FFF'F000u}, 0x1000u, "uboot-handoff"};

inline constexpr std::uint8_t kHandoffEnvCookie[4] = {0x21, 0x43, 0x65, 0x87};
inline constexpr std::uint8_t kHandoffSdramCookie[2] = {0x12, 0x57};
inline constexpr std::uint8_t kHandoffAltCfgCookie[3] = {0x34, 0x99, 0xBA};

inline constexpr std::uint32_t kHandoffRebootWarm = 0xBEEF'B001u;
inline constexpr std::size_t kHandoffSdramOffset = 0xF00;
inline constexpr std::size_t kHandoffAltCfgOffset = 0xF04;
inline constexpr std::size_t kHandoffRebootOffset = 0xF08;

inline constexpr std::size_t kHandoffEnvLimit = kHandoffSdramOffset;

inline constexpr std::uint16_t kHandoffSdramPresent = 0x8000;

class CycloneVProgrammer final : public hal::IFpgaProgrammer, public hal::IBootHandoff {
    TASTY_SEAT_EXEMPT(main);

public:
    [[nodiscard]] static Ex<CycloneVProgrammer> open(hal::BoardWindows& windows);

    [[nodiscard]] static Ex<CycloneVProgrammer> open_at(const char* manager_dir);

    static CycloneVProgrammer over(hal::RegisterWindow<FpgaMgr> mgr,
                                   hal::RegisterWindow<FpgaMgrData> data, hal::FpgaMemory handoff);

    CycloneVProgrammer(CycloneVProgrammer&& o) noexcept;
    CycloneVProgrammer& operator=(CycloneVProgrammer&&) = delete;

    [[nodiscard]] Ex<bool> program_begin() override;
    [[nodiscard]] Ex<void> program_chunk(std::span<const std::byte> chunk) override;
    [[nodiscard]] Ex<bool> program_end() override;
    [[nodiscard]] Ex<bool> program_step() override;
    [[nodiscard]] std::uint32_t pending_stage() noexcept override;

    [[nodiscard]] Ex<hal::HandoffPage> handoff() override;

    bool programmed() const override;

    hal::IBootHandoff* boot_handoff() noexcept override { return this; }

    [[nodiscard]] Ex<void> write_env(std::string_view core_name,
                                     std::span<const std::byte> cfg) override;

    [[nodiscard]] Ex<void> write_sdram_cfg(std::uint16_t cfg) override;

    [[nodiscard]] Ex<void> write_altcfg(std::uint8_t alt) override;

    [[nodiscard]] Ex<void> write_reboot_flag(bool warm) override;

private:
    CycloneVProgrammer(hal::RegisterWindow<FpgaMgr> mgr, hal::RegisterWindow<FpgaMgrData> data,
                       std::optional<hal::FpgaMemory> handoff);

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
    void set_cd_ratio(std::uint32_t ratio);
    void ctrl_update(std::uint32_t clear, std::uint32_t set);
    std::uint32_t mode();

    std::uint32_t ext_nibble();

    std::byte* handoff_base();

    std::optional<os::MmioRegion> mgr_pages_;
    std::optional<os::MmioRegion> data_pages_;
    std::optional<os::MmioRegion> handoff_pages_;

    hal::RegisterWindow<FpgaMgr> mgr_;
    hal::RegisterWindow<FpgaMgrData> data_;
    std::optional<hal::FpgaMemory> handoff_;

    std::optional<hal::RegisterWindow<FpgaMgrData>::Transaction> data_txn_;
    bool tail_seen_ = false;
    StepPc step_ = StepPc::Idle;

    std::array<hal::WindowClaim, 2> claims_{};
};

static_assert(hal::IFpgaProgrammer::kProgramStepBudget * FpgaMgr::kPollsPerPass ==
                  FpgaMgr::kTimeoutCount,
              "program() must spend exactly stock's poll budget per wait");

}  // namespace mister::boards
