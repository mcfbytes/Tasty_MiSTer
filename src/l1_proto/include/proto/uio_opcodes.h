// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
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
    Scanout57 = 0x57,
    Scanout59 = 0x59,
    Scanout5B = 0x5B,
    DmaWrite = 0x61,
    DmaRead = 0x62,
    DmaSdio = 0x63,
};

[[nodiscard]] constexpr hal::LinkWord word(UioOp op) noexcept {
    return hal::LinkWord{static_cast<std::uint16_t>(op)};
}

template <class WordSet>
[[nodiscard]] constexpr std::array<hal::LinkWord, 1 + WordSet::kCount> command_words(
    UioOp op,
    const WordSet& data
) noexcept {
    std::array<hal::LinkWord, 1 + WordSet::kCount> words{};
    words[0] = word(op);
    const std::span<const hal::LinkWord, WordSet::kCount> tail = data.data();
    for (std::size_t i = 0; i < tail.size(); ++i)
        words[i + 1] = tail[i];
    return words;
}

[[nodiscard]] constexpr std::uint8_t byte(UioOp op) noexcept {
    return static_cast<std::uint8_t>(std::to_underlying(op));
}

static_assert(word(UioOp::Status) == hal::LinkWord{0x00});
static_assert(word(UioOp::ButSw) == hal::LinkWord{0x01});
static_assert(word(UioOp::Joystick0) == hal::LinkWord{0x02});
static_assert(word(UioOp::Joystick1) == hal::LinkWord{0x03});
static_assert(word(UioOp::Mouse) == hal::LinkWord{0x04});
static_assert(word(UioOp::Keyboard) == hal::LinkWord{0x05});
static_assert(word(UioOp::KbdOsd) == hal::LinkWord{0x06});
static_assert(word(UioOp::Joystick2) == hal::LinkWord{0x10});
static_assert(word(UioOp::Joystick3) == hal::LinkWord{0x11});
static_assert(word(UioOp::Joystick4) == hal::LinkWord{0x12});
static_assert(word(UioOp::Joystick5) == hal::LinkWord{0x13});
static_assert(word(UioOp::GetString) == hal::LinkWord{0x14});
static_assert(word(UioOp::SetStatus) == hal::LinkWord{0x15});
static_assert(word(UioOp::GetSdStat) == hal::LinkWord{0x16});
static_assert(word(UioOp::SectorRd) == hal::LinkWord{0x17});
static_assert(word(UioOp::SectorWr) == hal::LinkWord{0x18});
static_assert(word(UioOp::SetSdConf) == hal::LinkWord{0x19});
static_assert(word(UioOp::Astick) == hal::LinkWord{0x1A});
static_assert(word(UioOp::SioIn) == hal::LinkWord{0x1B});
static_assert(word(UioOp::SetSdStat) == hal::LinkWord{0x1C});
static_assert(word(UioOp::SetSdInfo) == hal::LinkWord{0x1D});
static_assert(word(UioOp::SetStatus2) == hal::LinkWord{0x1E});
static_assert(word(UioOp::GetKbdLed) == hal::LinkWord{0x1F});
static_assert(word(UioOp::SetVideo) == hal::LinkWord{0x20});
static_assert(word(UioOp::Ps2Ctl) == hal::LinkWord{0x21});
static_assert(word(UioOp::Rtc) == hal::LinkWord{0x22});
static_assert(word(UioOp::GetVres) == hal::LinkWord{0x23});
static_assert(word(UioOp::Timestamp) == hal::LinkWord{0x24});
static_assert(word(UioOp::Leds) == hal::LinkWord{0x25});
static_assert(word(UioOp::AudVol) == hal::LinkWord{0x26});
static_assert(word(UioOp::SetHeight) == hal::LinkWord{0x27});
static_assert(word(UioOp::GetUartFlg) == hal::LinkWord{0x28});
static_assert(word(UioOp::GetStatus) == hal::LinkWord{0x29});
static_assert(word(UioOp::SetFltCoef) == hal::LinkWord{0x2A});
static_assert(word(UioOp::SetFltNum) == hal::LinkWord{0x2B});
static_assert(word(UioOp::GetVmode) == hal::LinkWord{0x2C});
static_assert(word(UioOp::SetVpos) == hal::LinkWord{0x2D});
static_assert(word(UioOp::GetOsdMask) == hal::LinkWord{0x2E});
static_assert(word(UioOp::SetFbuf) == hal::LinkWord{0x2F});
static_assert(word(UioOp::WaitVsync) == hal::LinkWord{0x30});
static_assert(word(UioOp::SetMemSz) == hal::LinkWord{0x31});
static_assert(word(UioOp::SetGamma) == hal::LinkWord{0x32});
static_assert(word(UioOp::SetGamCurv) == hal::LinkWord{0x33});
static_assert(word(UioOp::CdGet) == hal::LinkWord{0x34});
static_assert(word(UioOp::CdSet) == hal::LinkWord{0x35});
static_assert(word(UioOp::InfoGet) == hal::LinkWord{0x36});
static_assert(word(UioOp::SetWidth) == hal::LinkWord{0x37});
static_assert(word(UioOp::SetSync) == hal::LinkWord{0x38});
static_assert(word(UioOp::SetAfilter) == hal::LinkWord{0x39});
static_assert(word(UioOp::SetArCust) == hal::LinkWord{0x3A});
static_assert(word(UioOp::SetUart) == hal::LinkWord{0x3B});
static_assert(word(UioOp::ChkUpload) == hal::LinkWord{0x3C});
static_assert(word(UioOp::Astick2) == hal::LinkWord{0x3D});
static_assert(word(UioOp::ShadowMask) == hal::LinkWord{0x3E});
static_assert(word(UioOp::GetRumble) == hal::LinkWord{0x3F});
static_assert(word(UioOp::GetFbPar) == hal::LinkWord{0x40});
static_assert(word(UioOp::SetYcPar) == hal::LinkWord{0x41});
static_assert(word(UioOp::GetFrCnt) == hal::LinkWord{0x42});
static_assert(word(UioOp::GetF12Mod) == hal::LinkWord{0x43});
static_assert(word(UioOp::HdmiInt) == hal::LinkWord{0x44});
static_assert(word(UioOp::Scanout57) == hal::LinkWord{0x57});
static_assert(word(UioOp::Scanout59) == hal::LinkWord{0x59});
static_assert(word(UioOp::Scanout5B) == hal::LinkWord{0x5B});
static_assert(word(UioOp::DmaWrite) == hal::LinkWord{0x61});
static_assert(word(UioOp::DmaRead) == hal::LinkWord{0x62});
static_assert(word(UioOp::DmaSdio) == hal::LinkWord{0x63});

}
