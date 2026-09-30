// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/cyclone_v/cyclone_v_bridges.h"

#include <string_view>
#include <utility>

#include "boards/cyclone_v/cyclone_v_programmer.h"
#include "boards/cyclone_v/cyclone_v_windows.h"
#include "hal/board_windows.h"
#include "hal/boards_table.h"

namespace mister::boards {

using cyclone_v::Window;

consteval bool de10_row_matches(Window w, std::string_view name, os::PhysAddr transcribed) {
    const hal::WindowDecl& d = hal::window(hal::kDe10Profile, cyclone_v::id(w));
    return d.id == cyclone_v::id(w) && std::string_view(d.region.name) == name &&
           d.region.phys == transcribed;
}
static_assert(hal::kDe10Profile.windows.size() == cyclone_v::kWindowCount &&
                  de10_row_matches(Window::FpgaMgr, "fpga-mgr", kFpgaMgrBase) &&
                  de10_row_matches(Window::FpgaMgrData, "fpga-mgr-data", kFpgaMgrDataBase) &&
                  de10_row_matches(Window::Sdr, "sdr-ctl", kSdrBase) &&
                  de10_row_matches(Window::RstMgr, "rstmgr", kRstMgrBase) &&
                  de10_row_matches(Window::SysMgr, "sysmgr", kSysMgrBase) &&
                  de10_row_matches(Window::Nic301, "nic301", kNic301Base) &&
                  de10_row_matches(Window::LwBridge, "lw-bridge", hal::kLwBridgeBase) &&
                  hal::kDe10Profile.mailbox_window == cyclone_v::id(Window::FpgaMgr) &&
                  hal::kDe10Profile.lw_window == cyclone_v::id(Window::LwBridge),
              "the DE10 board row disagrees with the Cyclone V window names or bases");

CycloneVBridges::CycloneVBridges(hal::RegisterWindow<SdrCtl> sdr, hal::RegisterWindow<RstMgr> rst,
                                 hal::RegisterWindow<SysMgr> sys, hal::RegisterWindow<Nic301> nic)
    : sdr_(std::move(sdr)), rst_(std::move(rst)), sys_(std::move(sys)), nic_(std::move(nic)) {}

Ex<CycloneVBridges> CycloneVBridges::open(hal::BoardWindows& windows) {
    auto sdr = windows.take<SdrCtl>(cyclone_v::id(Window::Sdr));
    if (!sdr) return std::unexpected(sdr.error());
    auto rst = windows.take<RstMgr>(cyclone_v::id(Window::RstMgr));
    if (!rst) return std::unexpected(rst.error());
    auto sys = windows.take<SysMgr>(cyclone_v::id(Window::SysMgr));
    if (!sys) return std::unexpected(sys.error());
    auto nic = windows.take<Nic301>(cyclone_v::id(Window::Nic301));
    if (!nic) return std::unexpected(nic.error());
    CycloneVBridges b(std::move(sdr->regs), std::move(rst->regs), std::move(sys->regs),
                      std::move(nic->regs));
    b.claims_ = {std::move(sdr->claim), std::move(rst->claim), std::move(sys->claim),
                 std::move(nic->claim)};
    return b;
}

CycloneVBridges CycloneVBridges::over(hal::RegisterWindow<SdrCtl> sdr,
                                      hal::RegisterWindow<RstMgr> rst,
                                      hal::RegisterWindow<SysMgr> sys,
                                      hal::RegisterWindow<Nic301> nic) {
    return CycloneVBridges(std::move(sdr), std::move(rst), std::move(sys), std::move(nic));
}

void CycloneVBridges::apply(std::span<const BridgeStep> steps) {
    for (const BridgeStep& step : steps) {
        switch (step.block) {
            case BridgeBlock::Sdr: {
                hal::RegisterWindow<SdrCtl>::Transaction t(sdr_);
                sdr_.write(SdrCtl::Reg::FpgaPortRst, step.value);
                break;
            }
            case BridgeBlock::RstMgr: {
                hal::RegisterWindow<RstMgr>::Transaction t(rst_);
                rst_.write(RstMgr::Reg::BrgModReset, step.value);
                break;
            }
            case BridgeBlock::SysMgr: {
                hal::RegisterWindow<SysMgr>::Transaction t(sys_);
                sys_.write(SysMgr::Reg::FpgaIntfGrpModule, step.value);
                break;
            }
            case BridgeBlock::Nic301: {
                hal::RegisterWindow<Nic301>::Transaction t(nic_);
                nic_.write(Nic301::Reg::Remap, step.value);
                break;
            }
        }
    }
}

Ex<void> CycloneVBridges::pre_program() {
    apply(kDisableSequence);
    return {};
}

Ex<void> CycloneVBridges::post_program() {
    apply(kEnableSequence);
    return {};
}

Ex<void> CycloneVBridges::request_board_reset() {
    hal::RegisterWindow<RstMgr>::Transaction t(rst_);
    rst_.write(RstMgr::Reg::Ctrl, RstMgr::kResetRequest);
    return {};
}

}  // namespace mister::boards
