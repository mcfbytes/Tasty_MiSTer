// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "hal/bridge_sequencer.h"
#include "hal/register_window.h"
#include "hal/window_decl.h"
#include "infra/seat.h"

namespace mister::hal {
class BoardWindows;
}

namespace mister::boards {

struct SdrCtl {
    enum class Reg : std::uint32_t { FpgaPortRst = 0x5080 };
    static constexpr std::size_t kSize = 0x6000;
    static constexpr std::uint32_t kAllPortsEnable = 0x3FFF;
    static constexpr std::uint32_t kAllPortsReset = 0x0000;
};

struct RstMgr {
    enum class Reg : std::uint32_t {
        Ctrl = 0x04,
        BrgModReset = 0x1C,
    };
    static constexpr std::size_t kSize = 0x100;
    static constexpr std::uint32_t kOutOfReset = 0x0;
    static constexpr std::uint32_t kInReset = 0x7;

    static constexpr std::uint32_t kResetRequest = 0x1;
};
struct SysMgr {
    enum class Reg : std::uint32_t { FpgaIntfGrpModule = 0x28 };
    static constexpr std::size_t kSize = 0x100;
    static constexpr std::uint32_t kAllOff = 0x0;
};
struct Nic301 {
    enum class Reg : std::uint32_t { Remap = 0x00 };
    static constexpr std::size_t kSize = 0x100;
    static constexpr std::uint32_t kBridgesVisible = 0x19;
    static constexpr std::uint32_t kBridgesHidden = 0x01;
};

inline constexpr os::PhysAddr kSdrBase{0xFFC2'0000u};
inline constexpr os::PhysAddr kRstMgrBase{0xFFD0'5000u};
inline constexpr os::PhysAddr kSysMgrBase{0xFFD0'8000u};
inline constexpr os::PhysAddr kNic301Base{0xFF80'0000u};

enum class BridgeBlock : std::uint8_t { Sdr, RstMgr, SysMgr, Nic301 };

struct BridgeStep {
    BridgeBlock block;
    std::uint32_t value;
};

inline constexpr BridgeStep kDisableSequence[] = {
    {BridgeBlock::SysMgr, SysMgr::kAllOff},
    {BridgeBlock::Sdr, SdrCtl::kAllPortsReset},
    {BridgeBlock::RstMgr, RstMgr::kInReset},
    {BridgeBlock::Nic301, Nic301::kBridgesHidden},
};

inline constexpr BridgeStep kEnableSequence[] = {
    {BridgeBlock::Sdr, SdrCtl::kAllPortsEnable},
    {BridgeBlock::RstMgr, RstMgr::kOutOfReset},
    {BridgeBlock::Nic301, Nic301::kBridgesVisible},
};

class CycloneVBridges final : public hal::IBridgeSequencer {
    TASTY_SEAT_EXEMPT(main);

public:
    [[nodiscard]] static Ex<CycloneVBridges> open(hal::BoardWindows& windows);

    static CycloneVBridges over(hal::RegisterWindow<SdrCtl> sdr, hal::RegisterWindow<RstMgr> rst,
                                hal::RegisterWindow<SysMgr> sys, hal::RegisterWindow<Nic301> nic);

    [[nodiscard]] Ex<void> pre_program() override;
    [[nodiscard]] Ex<void> post_program() override;

    [[nodiscard]] Ex<void> request_board_reset() override;

private:
    CycloneVBridges() = default;
    CycloneVBridges(hal::RegisterWindow<SdrCtl> sdr, hal::RegisterWindow<RstMgr> rst,
                    hal::RegisterWindow<SysMgr> sys, hal::RegisterWindow<Nic301> nic);
    void apply(std::span<const BridgeStep> steps);

    hal::RegisterWindow<SdrCtl> sdr_;
    hal::RegisterWindow<RstMgr> rst_;
    hal::RegisterWindow<SysMgr> sys_;
    hal::RegisterWindow<Nic301> nic_;

    std::array<hal::WindowClaim, 4> claims_{};
};

}  // namespace mister::boards
