// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <span>

#include "cores/core_init_context.h"
#include "cores/core_init_host.h"
#include "cores/core_init_step.h"
#include "cores/init_step.h"

namespace mister::app {

using cores::CoreInitContext;
using cores::CoreInitHost;
using cores::CoreInitStep;
using cores::CoreTypeMask;
using cores::FabricReg;
using cores::InitPhase;
using cores::InitStatus;
using cores::OnError;
using cores::kAnyDialect;
using cores::kEightBitOnly;

[[nodiscard]] Ex<void> init_reset_video_geometry(CoreInitContext& ctx);
[[nodiscard]] Ex<void> init_arm_video_prelude(CoreInitContext& ctx);
[[nodiscard]] Ex<void> init_flush_status(CoreInitContext& ctx);
[[nodiscard]] Ex<void> init_publish_identity(CoreInitContext& ctx);

inline constexpr std::string_view kGapIdleGuardedDigital =
    "M2 F1 - the only emitter is JoystickPort::push_digital, "
    "and JoystickPort::submit's edge guard returns early "
    "for an idle pad. STOCK IS IDENTICALLY GUARDED "
    "(input.cpp:6380), so closing this is a DIVERGENCE with its "
    "own ledger row and a pass of its own";
inline constexpr std::string_view kGapIdleGuardedAnalogL =
    "M2 F1 - the only emitter is JoystickPort::push_analog_l, "
    "and JoystickPort::submit's edge guard returns early "
    "for an idle pad. STOCK IS IDENTICALLY GUARDED "
    "(input.cpp:6380), so closing this is a DIVERGENCE with its "
    "own ledger row and a pass of its own";
inline constexpr std::string_view kGapIdleGuardedAnalogR =
    "M2 F1 - the only emitter is JoystickPort::push_analog_r, "
    "and JoystickPort::submit's edge guard returns early "
    "for an idle pad. STOCK IS IDENTICALLY GUARDED "
    "(input.cpp:6380), so closing this is a DIVERGENCE with its "
    "own ledger row and a pass of its own";
inline constexpr std::string_view kGapIdleGuardedPaddle =
    "M2 F1 - the only emitter is JoystickPort::push_paddle, "
    "and JoystickPort::submit's edge guard returns early "
    "for an idle pad. STOCK IS IDENTICALLY GUARDED "
    "(input.cpp:6380), so closing this is a DIVERGENCE with its "
    "own ledger row and a pass of its own";
inline constexpr std::string_view kGapIdleGuardedSpinner =
    "M2 F1 - the only emitter is JoystickPort::push_spinner, "
    "and JoystickPort::submit's edge guard returns early "
    "for an idle pad. STOCK IS IDENTICALLY GUARDED "
    "(input.cpp:6380), so closing this is a DIVERGENCE with its "
    "own ledger row and a pass of its own";

inline constexpr std::size_t kCoreInitStepCount = 46;

inline constexpr std::array<CoreInitStep, kCoreInitStepCount> kCoreInitBase{{
    CoreInitStep{"video.reset_geometry", FabricReg::None, InitStatus::Satisfied,
                 InitPhase::CompletionArm, kAnyDialect, &init_reset_video_geometry, OnError::Ignore,
                 "item/item - host state only; VideoWire::geo_gate_ "
                 "re-armed per core"},
    CoreInitStep{"video.arm_prelude", FabricReg::None, InitStatus::Satisfied,
                 InitPhase::CompletionArm, kAnyDialect, &init_arm_video_prelude, OnError::Ignore,
                 "item - arms the 8-opcode prelude; wire-silent at the arm"},
    CoreInitStep{"status.flush", FabricReg::Status, InitStatus::Satisfied, InitPhase::CompletionArm,
                 kEightBitOnly, &init_flush_status, OnError::Ignore,
                 "item - StatusRegister::flush, all 8 words; req "
                 "dialect gate"},
    CoreInitStep{"identity.publish", FabricReg::None, InitStatus::Satisfied,
                 InitPhase::CompletionArm, kAnyDialect, &init_publish_identity, OnError::Ignore,
                 "item/video-rule - the T-RT identity latch T-UI reads on the "
                 "CoreLoaded edge"},
    CoreInitStep{"negotiate.sdram_sz", FabricReg::SdramSz, InitStatus::SatisfiedElsewhere,
                 InitPhase::Negotiation, kEightBitOnly, nullptr, OnError::Ignore,
                 "CoreSession::set_memory_size (0x31), EightBit only"},
    CoreInitStep{"apply.rtc", FabricReg::Rtc, InitStatus::SatisfiedElsewhere, InitPhase::Apply,
                 kEightBitOnly, nullptr, OnError::Ignore,
                 "CoreSession::send_rtc (0x22), 4 words, EightBit only"},
    CoreInitStep{"apply.timestamp", FabricReg::Timestamp, InitStatus::SatisfiedElsewhere,
                 InitPhase::Apply, kEightBitOnly, nullptr, OnError::Ignore,
                 "CoreSession::send_rtc (0x24), 2 words, EightBit only"},
    CoreInitStep{"apply.uart_mode", FabricReg::UartMode, InitStatus::SatisfiedElsewhere,
                 InitPhase::Apply, kAnyDialect, nullptr, OnError::Ignore,
                 "uart_mode.cpp:196 via the unconditional set_mode(None) at "
                 ":372"},
    CoreInitStep{"apply.uart_speed", FabricReg::UartSpeed, InitStatus::SatisfiedElsewhere,
                 InitPhase::Apply, kAnyDialect, nullptr, OnError::Ignore,
                 "uart_mode.cpp:196 - same 0x3B commit window"},
    CoreInitStep{"late.cfg", FabricReg::Cfg, InitStatus::WrittenLate, InitPhase::PostLoad,
                 kAnyDialect, nullptr, OnError::Ignore,
                 "M2 F3 - VideoWire::step_prelude S6 (0x01), ~68 ms AFTER "
                 "CoreLoaded; stock sends it at user_io.cpp:1518, before the "
                 "reset release at :1696"},
    CoreInitStep{"late.systop_cfg", FabricReg::SysTopCfg, InitStatus::WrittenLate,
                 InitPhase::PostLoad, kAnyDialect, nullptr, OnError::Ignore,
                 "M2 F3 - the same 0x01 window loads sys_top.v:288's separate "
                 "cfg register"},
    CoreInitStep{"late.scaler_flt", FabricReg::ScalerFlt, InitStatus::WrittenLate,
                 InitPhase::PostLoad, kAnyDialect, nullptr, OnError::Ignore,
                 "M2 F4 - VideoWire::step_prelude S2 (0x2B), after "
                 "CoreLoaded"},
    CoreInitStep{"late.img_size", FabricReg::ImgSize, InitStatus::WrittenLate, InitPhase::PostLoad,
                 kAnyDialect, nullptr, OnError::Ignore,
                 "block_slots.cpp:162 (0x1D) - mount "
                 "only, never on a plain core load"},
    CoreInitStep{"late.ioctl_index", FabricReg::IoctlIndex, InitStatus::WrittenLate,
                 InitPhase::PostLoad, kAnyDialect, nullptr, OnError::Ignore,
                 "download_session.cpp:131 (0x55) - download only"},
    CoreInitStep{"late.ioctl_file_ext", FabricReg::IoctlFileExt, InitStatus::WrittenLate,
                 InitPhase::PostLoad, kAnyDialect, nullptr, OnError::Ignore,
                 "download_session.cpp:171 (0x56) - download only"},
    CoreInitStep{"gap.joystick_0", FabricReg::Joystick0, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedDigital},
    CoreInitStep{"gap.joystick_1", FabricReg::Joystick1, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedDigital},
    CoreInitStep{"gap.joystick_2", FabricReg::Joystick2, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedDigital},
    CoreInitStep{"gap.joystick_3", FabricReg::Joystick3, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedDigital},
    CoreInitStep{"gap.joystick_4", FabricReg::Joystick4, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedDigital},
    CoreInitStep{"gap.joystick_5", FabricReg::Joystick5, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedDigital},
    CoreInitStep{"gap.joystick_l_analog_0", FabricReg::JoystickLAnalog0, InitStatus::DeclaredGap,
                 InitPhase::Never, kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedAnalogL},
    CoreInitStep{"gap.joystick_l_analog_1", FabricReg::JoystickLAnalog1, InitStatus::DeclaredGap,
                 InitPhase::Never, kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedAnalogL},
    CoreInitStep{"gap.joystick_l_analog_2", FabricReg::JoystickLAnalog2, InitStatus::DeclaredGap,
                 InitPhase::Never, kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedAnalogL},
    CoreInitStep{"gap.joystick_l_analog_3", FabricReg::JoystickLAnalog3, InitStatus::DeclaredGap,
                 InitPhase::Never, kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedAnalogL},
    CoreInitStep{"gap.joystick_l_analog_4", FabricReg::JoystickLAnalog4, InitStatus::DeclaredGap,
                 InitPhase::Never, kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedAnalogL},
    CoreInitStep{"gap.joystick_l_analog_5", FabricReg::JoystickLAnalog5, InitStatus::DeclaredGap,
                 InitPhase::Never, kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedAnalogL},
    CoreInitStep{"gap.joystick_r_analog_0", FabricReg::JoystickRAnalog0, InitStatus::DeclaredGap,
                 InitPhase::Never, kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedAnalogR},
    CoreInitStep{"gap.joystick_r_analog_1", FabricReg::JoystickRAnalog1, InitStatus::DeclaredGap,
                 InitPhase::Never, kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedAnalogR},
    CoreInitStep{"gap.joystick_r_analog_2", FabricReg::JoystickRAnalog2, InitStatus::DeclaredGap,
                 InitPhase::Never, kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedAnalogR},
    CoreInitStep{"gap.joystick_r_analog_3", FabricReg::JoystickRAnalog3, InitStatus::DeclaredGap,
                 InitPhase::Never, kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedAnalogR},
    CoreInitStep{"gap.joystick_r_analog_4", FabricReg::JoystickRAnalog4, InitStatus::DeclaredGap,
                 InitPhase::Never, kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedAnalogR},
    CoreInitStep{"gap.joystick_r_analog_5", FabricReg::JoystickRAnalog5, InitStatus::DeclaredGap,
                 InitPhase::Never, kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedAnalogR},
    CoreInitStep{"gap.paddle_0", FabricReg::Paddle0, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedPaddle},
    CoreInitStep{"gap.paddle_1", FabricReg::Paddle1, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedPaddle},
    CoreInitStep{"gap.paddle_2", FabricReg::Paddle2, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedPaddle},
    CoreInitStep{"gap.paddle_3", FabricReg::Paddle3, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedPaddle},
    CoreInitStep{"gap.paddle_4", FabricReg::Paddle4, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedPaddle},
    CoreInitStep{"gap.paddle_5", FabricReg::Paddle5, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedPaddle},
    CoreInitStep{"gap.spinner_0", FabricReg::Spinner0, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedSpinner},
    CoreInitStep{"gap.spinner_1", FabricReg::Spinner1, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedSpinner},
    CoreInitStep{"gap.spinner_2", FabricReg::Spinner2, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedSpinner},
    CoreInitStep{"gap.spinner_3", FabricReg::Spinner3, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedSpinner},
    CoreInitStep{"gap.spinner_4", FabricReg::Spinner4, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedSpinner},
    CoreInitStep{"gap.spinner_5", FabricReg::Spinner5, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore, kGapIdleGuardedSpinner},
    CoreInitStep{"gap.gamma_en", FabricReg::GammaEn, InitStatus::DeclaredGap, InitPhase::Never,
                 kAnyDialect, nullptr, OnError::Ignore,
                 "M2 F2 - tasty's only 0x32 emitter is "
                 "video_service.cpp:1487's ZERO-PAYLOAD probe, and "
                 "hps_io.sv:528 writes gamma_en in the payload branch only. "
                 "Stock has the same hole (video.cpp:702 early-returns with "
                 "no <core>_gamma.cfg)"},
}};

[[nodiscard]] Ex<void> run_core_init(CoreInitContext& ctx,
                                     std::span<const CoreInitStep> additions = {});

}  // namespace mister::app
