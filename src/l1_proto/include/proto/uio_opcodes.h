// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "hal/types.h"

namespace mister::proto {

enum class UioOp : std::uint16_t {

    Status = 0x00,
    ButSw = 0x01,
    Joystick0 = 0x02,
    Joystick1 = 0x03,
    Mouse = 0x04,
    Keyboard = 0x05,
    KbdOsd = 0x06,
    Joystick2 = 0x10,
    Joystick3 = 0x11,
    Joystick4 = 0x12,
    Joystick5 = 0x13,
    GetString = 0x14,
    SetStatus = 0x15,
    GetSdStat = 0x16,
    SectorRd = 0x17,
    SectorWr = 0x18,
    SetSdConf = 0x19,
    Astick = 0x1A,
    SioIn = 0x1B,
    SetSdStat = 0x1C,
    SetSdInfo = 0x1D,
    SetStatus2 = 0x1E,
    GetKbdLed = 0x1F,
    SetVideo = 0x20,
    Ps2Ctl = 0x21,
    Rtc = 0x22,
    GetVres = 0x23,
    Timestamp = 0x24,
    Leds = 0x25,
    AudVol = 0x26,
    SetHeight = 0x27,
    GetUartFlg = 0x28,
    GetStatus = 0x29,
    SetFltCoef = 0x2A,
    SetFltNum = 0x2B,
    GetVmode = 0x2C,
    SetVpos = 0x2D,
    GetOsdMask = 0x2E,
    SetFbuf = 0x2F,
    WaitVsync = 0x30,
    SetMemSz = 0x31,
    SetGamma = 0x32,
    SetGamCurv = 0x33,
    CdGet = 0x34,
    CdSet = 0x35,
    InfoGet = 0x36,
    SetWidth = 0x37,
    SetSync = 0x38,
    SetAfilter = 0x39,
    SetArCust = 0x3A,
    SetUart = 0x3B,
    ChkUpload = 0x3C,
    Astick2 = 0x3D,
    ShadowMask = 0x3E,
    GetRumble = 0x3F,
    GetFbPar = 0x40,
    SetYcPar = 0x41,
    GetFrCnt = 0x42,
    GetF12Mod = 0x43,
    HdmiInt = 0x44,
    DmaWrite = 0x61,
    DmaRead = 0x62,
    DmaSdio = 0x63,

    FileTx = 0x53,
    FileTxDat = 0x54,
    FileIndex = 0x55,
    FileInfo = 0x56,
    SharpmzFileAddr = 0x57,
    SharpmzFileAddrTx = 0x58,
    SharpmzFileAddrRx = 0x59,
    SharpmzConfigRx = 0x5A,
    SharpmzConfigTx = 0x5B,

    OsdWrite = 0x20,
    OsdWriteLatch = 0x28,
    OsdDisable = 0x40,
    OsdEnable = 0x41,
    OsdEnableInfo = 0x45,
};

[[nodiscard]] constexpr hal::SpiWord word(UioOp op) noexcept {
    return hal::SpiWord{static_cast<std::uint16_t>(op)};
}

[[nodiscard]] constexpr std::uint8_t byte(UioOp op) noexcept {
    return static_cast<std::uint8_t>(std::to_underlying(op));
}

static_assert(std::to_underlying(UioOp::Status) == 0x00);
static_assert(std::to_underlying(UioOp::ButSw) == 0x01);
static_assert(std::to_underlying(UioOp::Joystick0) == 0x02);
static_assert(std::to_underlying(UioOp::Joystick1) == 0x03);
static_assert(std::to_underlying(UioOp::Mouse) == 0x04);
static_assert(std::to_underlying(UioOp::Keyboard) == 0x05);
static_assert(std::to_underlying(UioOp::KbdOsd) == 0x06);
static_assert(std::to_underlying(UioOp::Joystick2) == 0x10);
static_assert(std::to_underlying(UioOp::Joystick3) == 0x11);
static_assert(std::to_underlying(UioOp::Joystick4) == 0x12);
static_assert(std::to_underlying(UioOp::Joystick5) == 0x13);
static_assert(std::to_underlying(UioOp::GetString) == 0x14);
static_assert(std::to_underlying(UioOp::SetStatus) == 0x15);
static_assert(std::to_underlying(UioOp::GetSdStat) == 0x16);
static_assert(std::to_underlying(UioOp::SectorRd) == 0x17);
static_assert(std::to_underlying(UioOp::SectorWr) == 0x18);
static_assert(std::to_underlying(UioOp::SetSdConf) == 0x19);
static_assert(std::to_underlying(UioOp::Astick) == 0x1A);
static_assert(std::to_underlying(UioOp::SioIn) == 0x1B);
static_assert(std::to_underlying(UioOp::SetSdStat) == 0x1C);
static_assert(std::to_underlying(UioOp::SetSdInfo) == 0x1D);
static_assert(std::to_underlying(UioOp::SetStatus2) == 0x1E);
static_assert(std::to_underlying(UioOp::GetKbdLed) == 0x1F);
static_assert(std::to_underlying(UioOp::SetVideo) == 0x20);
static_assert(std::to_underlying(UioOp::Ps2Ctl) == 0x21);
static_assert(std::to_underlying(UioOp::Rtc) == 0x22);
static_assert(std::to_underlying(UioOp::GetVres) == 0x23);
static_assert(std::to_underlying(UioOp::Timestamp) == 0x24);
static_assert(std::to_underlying(UioOp::Leds) == 0x25);
static_assert(std::to_underlying(UioOp::AudVol) == 0x26);
static_assert(std::to_underlying(UioOp::SetHeight) == 0x27);
static_assert(std::to_underlying(UioOp::GetUartFlg) == 0x28);
static_assert(std::to_underlying(UioOp::GetStatus) == 0x29);
static_assert(std::to_underlying(UioOp::SetFltCoef) == 0x2A);
static_assert(std::to_underlying(UioOp::SetFltNum) == 0x2B);
static_assert(std::to_underlying(UioOp::GetVmode) == 0x2C);
static_assert(std::to_underlying(UioOp::SetVpos) == 0x2D);
static_assert(std::to_underlying(UioOp::GetOsdMask) == 0x2E);
static_assert(std::to_underlying(UioOp::SetFbuf) == 0x2F);
static_assert(std::to_underlying(UioOp::WaitVsync) == 0x30);
static_assert(std::to_underlying(UioOp::SetMemSz) == 0x31);
static_assert(std::to_underlying(UioOp::SetGamma) == 0x32);
static_assert(std::to_underlying(UioOp::SetGamCurv) == 0x33);
static_assert(std::to_underlying(UioOp::CdGet) == 0x34);
static_assert(std::to_underlying(UioOp::CdSet) == 0x35);
static_assert(std::to_underlying(UioOp::InfoGet) == 0x36);
static_assert(std::to_underlying(UioOp::SetWidth) == 0x37);
static_assert(std::to_underlying(UioOp::SetSync) == 0x38);
static_assert(std::to_underlying(UioOp::SetAfilter) == 0x39);
static_assert(std::to_underlying(UioOp::SetArCust) == 0x3A);
static_assert(std::to_underlying(UioOp::SetUart) == 0x3B);
static_assert(std::to_underlying(UioOp::ChkUpload) == 0x3C);
static_assert(std::to_underlying(UioOp::Astick2) == 0x3D);
static_assert(std::to_underlying(UioOp::ShadowMask) == 0x3E);
static_assert(std::to_underlying(UioOp::GetRumble) == 0x3F);
static_assert(std::to_underlying(UioOp::GetFbPar) == 0x40);
static_assert(std::to_underlying(UioOp::SetYcPar) == 0x41);
static_assert(std::to_underlying(UioOp::GetFrCnt) == 0x42);
static_assert(std::to_underlying(UioOp::GetF12Mod) == 0x43);
static_assert(std::to_underlying(UioOp::HdmiInt) == 0x44);
static_assert(std::to_underlying(UioOp::DmaWrite) == 0x61);
static_assert(std::to_underlying(UioOp::DmaRead) == 0x62);
static_assert(std::to_underlying(UioOp::DmaSdio) == 0x63);
static_assert(std::to_underlying(UioOp::FileTx) == 0x53);
static_assert(std::to_underlying(UioOp::FileTxDat) == 0x54);
static_assert(std::to_underlying(UioOp::FileIndex) == 0x55);
static_assert(std::to_underlying(UioOp::FileInfo) == 0x56);
static_assert(std::to_underlying(UioOp::SharpmzFileAddr) == 0x57);
static_assert(std::to_underlying(UioOp::SharpmzFileAddrTx) == 0x58);
static_assert(std::to_underlying(UioOp::SharpmzFileAddrRx) == 0x59);
static_assert(std::to_underlying(UioOp::SharpmzConfigRx) == 0x5A);
static_assert(std::to_underlying(UioOp::SharpmzConfigTx) == 0x5B);
static_assert(std::to_underlying(UioOp::OsdWrite) == 0x20);
static_assert(std::to_underlying(UioOp::OsdWriteLatch) == 0x28);
static_assert(std::to_underlying(UioOp::OsdDisable) == 0x40);
static_assert(std::to_underlying(UioOp::OsdEnable) == 0x41);
static_assert(std::to_underlying(UioOp::OsdEnableInfo) == 0x45);

}
