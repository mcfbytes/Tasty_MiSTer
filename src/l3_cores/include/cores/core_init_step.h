// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "cores/types.h"
#include "infra/error.h"

namespace mister::cores {

struct CoreInitContext;

enum class FabricReg : std::uint8_t {
    None = 0,

    Joystick0,
    Joystick1,
    Joystick2,
    Joystick3,
    Joystick4,
    Joystick5,
    JoystickLAnalog0,
    JoystickLAnalog1,
    JoystickLAnalog2,
    JoystickLAnalog3,
    JoystickLAnalog4,
    JoystickLAnalog5,
    JoystickRAnalog0,
    JoystickRAnalog1,
    JoystickRAnalog2,
    JoystickRAnalog3,
    JoystickRAnalog4,
    JoystickRAnalog5,
    Paddle0,
    Paddle1,
    Paddle2,
    Paddle3,
    Paddle4,
    Paddle5,
    Spinner0,
    Spinner1,
    Spinner2,
    Spinner3,
    Spinner4,
    Spinner5,

    Cfg,
    Status,
    GammaEn,
    ImgSize,
    IoctlIndex,
    IoctlFileExt,
    SdramSz,
    Rtc,
    Timestamp,
    UartMode,
    UartSpeed,

    SysTopCfg,
    ScalerFlt,
};

enum class InitStatus : std::uint8_t {

    Satisfied,

    SatisfiedElsewhere,

    WrittenLate,

    DeclaredGap,
};

enum class InitPhase : std::uint8_t {
    Negotiation,
    Apply,
    CompletionArm,
    PostLoad,
    Never,
};

enum class OnError : std::uint8_t {
    Ignore,
    Fail,
};

struct CoreInitStep {
    std::string_view name;
    FabricReg reg = FabricReg::None;
    InitStatus status = InitStatus::DeclaredGap;
    InitPhase phase = InitPhase::Never;
    CoreTypeMask applies_to = kAnyDialect;

    Ex<void> (*fn)(CoreInitContext&) = nullptr;
    OnError on_error = OnError::Ignore;

    std::string_view site;
};

}  // namespace mister::cores
