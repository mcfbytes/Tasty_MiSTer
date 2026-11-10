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
using cores::ICoreInitHost;
using cores::CoreInitStep;
using cores::CoreTypeMask;
using cores::FabricReg;
using cores::InitPhase;
using cores::InitStatus;
using cores::OnError;
using cores::kEightBitDialect;

[[nodiscard]] Ex<void> init_reset_video_geometry(CoreInitContext& ctx);
[[nodiscard]] Ex<void> init_arm_video_prelude(CoreInitContext& ctx);
[[nodiscard]] Ex<void> init_flush_status(CoreInitContext& ctx);
[[nodiscard]] Ex<void> init_publish_identity(CoreInitContext& ctx);

inline constexpr std::string_view
    kGapIdleGuardedDigital = "M2 F1 - the only emitter is JoystickPort::push_digital, "
                             "and JoystickPort::submit's edge guard returns early "
                             "for an idle pad. STOCK IS IDENTICALLY GUARDED "
                             "(input.cpp:6380), so closing this is a DIVERGENCE with its "
                             "own ledger row and a pass of its own";
inline constexpr std::string_view
    kGapIdleGuardedAnalogL = "M2 F1 - the only emitter is JoystickPort::push_analog_l, "
                             "and JoystickPort::submit's edge guard returns early "
                             "for an idle pad. STOCK IS IDENTICALLY GUARDED "
                             "(input.cpp:6380), so closing this is a DIVERGENCE with its "
                             "own ledger row and a pass of its own";
inline constexpr std::string_view
    kGapIdleGuardedAnalogR = "M2 F1 - the only emitter is JoystickPort::push_analog_r, "
                             "and JoystickPort::submit's edge guard returns early "
                             "for an idle pad. STOCK IS IDENTICALLY GUARDED "
                             "(input.cpp:6380), so closing this is a DIVERGENCE with its "
                             "own ledger row and a pass of its own";
inline constexpr std::string_view
    kGapIdleGuardedPaddle = "M2 F1 - the only emitter is JoystickPort::push_paddle, "
                            "and JoystickPort::submit's edge guard returns early "
                            "for an idle pad. STOCK IS IDENTICALLY GUARDED "
                            "(input.cpp:6380), so closing this is a DIVERGENCE with its "
                            "own ledger row and a pass of its own";
inline constexpr std::string_view
    kGapIdleGuardedSpinner = "M2 F1 - the only emitter is JoystickPort::push_spinner, "
                             "and JoystickPort::submit's edge guard returns early "
                             "for an idle pad. STOCK IS IDENTICALLY GUARDED "
                             "(input.cpp:6380), so closing this is a DIVERGENCE with its "
                             "own ledger row and a pass of its own";

inline constexpr auto kCoreInitBase = std::to_array<CoreInitStep>({
    {.name = "video.reset_geometry",
     .status = InitStatus::Satisfied,
     .phase = InitPhase::CompletionArm,
     .fn = &init_reset_video_geometry,
     .site = "item/item - host state only; VideoWire::geo_gate_ "
             "re-armed per core"},
    {.name = "video.arm_prelude",
     .status = InitStatus::Satisfied,
     .phase = InitPhase::CompletionArm,
     .fn = &init_arm_video_prelude,
     .site = "item - arms the 8-opcode prelude; wire-silent at the arm"},
    {.name = "status.flush",
     .reg = FabricReg::Status,
     .status = InitStatus::Satisfied,
     .phase = InitPhase::CompletionArm,
     .applies_to = kEightBitDialect,
     .fn = &init_flush_status,
     .site = "item - StatusRegister::flush, all 8 words; req "
             "dialect gate"},
    {.name = "identity.publish",
     .status = InitStatus::Satisfied,
     .phase = InitPhase::CompletionArm,
     .fn = &init_publish_identity,
     .site = "item/video-rule - the T-RT identity latch T-UI reads on the "
             "CoreLoaded edge"},
    {.name = "negotiate.sdram_sz",
     .reg = FabricReg::SdramSz,
     .status = InitStatus::SatisfiedElsewhere,
     .phase = InitPhase::Negotiation,
     .applies_to = kEightBitDialect,
     .site = "CoreSession::set_memory_size (0x31), EightBit only"},
    {.name = "apply.rtc",
     .reg = FabricReg::Rtc,
     .status = InitStatus::SatisfiedElsewhere,
     .phase = InitPhase::Apply,
     .applies_to = kEightBitDialect,
     .site = "CoreSession::send_rtc_image (0x22), 4 words, EightBit only"},
    {.name = "apply.timestamp",
     .reg = FabricReg::Timestamp,
     .status = InitStatus::SatisfiedElsewhere,
     .phase = InitPhase::Apply,
     .applies_to = kEightBitDialect,
     .site = "CoreSession::send_rtc_image (0x24), 2 words, EightBit only"},
    {.name = "apply.uart_mode",
     .reg = FabricReg::UartMode,
     .status = InitStatus::SatisfiedElsewhere,
     .phase = InitPhase::Apply,
     .site = "uart_mode.cpp:196 via the unconditional set_mode(None) at :372"},
    {.name = "apply.uart_speed",
     .reg = FabricReg::UartSpeed,
     .status = InitStatus::SatisfiedElsewhere,
     .phase = InitPhase::Apply,
     .site = "uart_mode.cpp:196 - same 0x3B commit window"},
    {.name = "late.cfg",
     .reg = FabricReg::Cfg,
     .status = InitStatus::WrittenLate,
     .phase = InitPhase::PostLoad,
     .site = "M2 F3 - VideoWire::step_prelude S6 (0x01), ~68 ms AFTER "
             "CoreLoaded; stock sends it at user_io.cpp:1518, before the "
             "reset release at :1696"},
    {.name = "late.systop_cfg",
     .reg = FabricReg::SysTopCfg,
     .status = InitStatus::WrittenLate,
     .phase = InitPhase::PostLoad,
     .site = "M2 F3 - the same 0x01 window loads sys_top.v:288's separate cfg register"},
    {.name = "late.scaler_flt",
     .reg = FabricReg::ScalerFlt,
     .status = InitStatus::WrittenLate,
     .phase = InitPhase::PostLoad,
     .site = "M2 F4 - VideoWire::step_prelude S2 (0x2B), after CoreLoaded"},
    {.name = "late.img_size",
     .reg = FabricReg::ImgSize,
     .status = InitStatus::WrittenLate,
     .phase = InitPhase::PostLoad,
     .site = "block_slots.cpp:162 (0x1D) - mount only, never on a plain core load"},
    {.name = "late.ioctl_index",
     .reg = FabricReg::IoctlIndex,
     .status = InitStatus::WrittenLate,
     .phase = InitPhase::PostLoad,
     .site = "download_session.cpp:131 (0x55) - download only"},
    {.name = "late.ioctl_file_ext",
     .reg = FabricReg::IoctlFileExt,
     .status = InitStatus::WrittenLate,
     .phase = InitPhase::PostLoad,
     .site = "download_session.cpp:171 (0x56) - download only"},
    {.name = "gap.joystick_0", .reg = FabricReg::Joystick0, .site = kGapIdleGuardedDigital},
    {.name = "gap.joystick_1", .reg = FabricReg::Joystick1, .site = kGapIdleGuardedDigital},
    {.name = "gap.joystick_2", .reg = FabricReg::Joystick2, .site = kGapIdleGuardedDigital},
    {.name = "gap.joystick_3", .reg = FabricReg::Joystick3, .site = kGapIdleGuardedDigital},
    {.name = "gap.joystick_4", .reg = FabricReg::Joystick4, .site = kGapIdleGuardedDigital},
    {.name = "gap.joystick_5", .reg = FabricReg::Joystick5, .site = kGapIdleGuardedDigital},
    {.name = "gap.joystick_l_analog_0",
     .reg = FabricReg::JoystickLAnalog0,
     .site = kGapIdleGuardedAnalogL},
    {.name = "gap.joystick_l_analog_1",
     .reg = FabricReg::JoystickLAnalog1,
     .site = kGapIdleGuardedAnalogL},
    {.name = "gap.joystick_l_analog_2",
     .reg = FabricReg::JoystickLAnalog2,
     .site = kGapIdleGuardedAnalogL},
    {.name = "gap.joystick_l_analog_3",
     .reg = FabricReg::JoystickLAnalog3,
     .site = kGapIdleGuardedAnalogL},
    {.name = "gap.joystick_l_analog_4",
     .reg = FabricReg::JoystickLAnalog4,
     .site = kGapIdleGuardedAnalogL},
    {.name = "gap.joystick_l_analog_5",
     .reg = FabricReg::JoystickLAnalog5,
     .site = kGapIdleGuardedAnalogL},
    {.name = "gap.joystick_r_analog_0",
     .reg = FabricReg::JoystickRAnalog0,
     .site = kGapIdleGuardedAnalogR},
    {.name = "gap.joystick_r_analog_1",
     .reg = FabricReg::JoystickRAnalog1,
     .site = kGapIdleGuardedAnalogR},
    {.name = "gap.joystick_r_analog_2",
     .reg = FabricReg::JoystickRAnalog2,
     .site = kGapIdleGuardedAnalogR},
    {.name = "gap.joystick_r_analog_3",
     .reg = FabricReg::JoystickRAnalog3,
     .site = kGapIdleGuardedAnalogR},
    {.name = "gap.joystick_r_analog_4",
     .reg = FabricReg::JoystickRAnalog4,
     .site = kGapIdleGuardedAnalogR},
    {.name = "gap.joystick_r_analog_5",
     .reg = FabricReg::JoystickRAnalog5,
     .site = kGapIdleGuardedAnalogR},
    {.name = "gap.paddle_0", .reg = FabricReg::Paddle0, .site = kGapIdleGuardedPaddle},
    {.name = "gap.paddle_1", .reg = FabricReg::Paddle1, .site = kGapIdleGuardedPaddle},
    {.name = "gap.paddle_2", .reg = FabricReg::Paddle2, .site = kGapIdleGuardedPaddle},
    {.name = "gap.paddle_3", .reg = FabricReg::Paddle3, .site = kGapIdleGuardedPaddle},
    {.name = "gap.paddle_4", .reg = FabricReg::Paddle4, .site = kGapIdleGuardedPaddle},
    {.name = "gap.paddle_5", .reg = FabricReg::Paddle5, .site = kGapIdleGuardedPaddle},
    {.name = "gap.spinner_0", .reg = FabricReg::Spinner0, .site = kGapIdleGuardedSpinner},
    {.name = "gap.spinner_1", .reg = FabricReg::Spinner1, .site = kGapIdleGuardedSpinner},
    {.name = "gap.spinner_2", .reg = FabricReg::Spinner2, .site = kGapIdleGuardedSpinner},
    {.name = "gap.spinner_3", .reg = FabricReg::Spinner3, .site = kGapIdleGuardedSpinner},
    {.name = "gap.spinner_4", .reg = FabricReg::Spinner4, .site = kGapIdleGuardedSpinner},
    {.name = "gap.spinner_5", .reg = FabricReg::Spinner5, .site = kGapIdleGuardedSpinner},
    {.name = "gap.gamma_en",
     .reg = FabricReg::GammaEn,
     .site = "M2 F2 - tasty's only 0x32 emitter is "
             "ProbeOps::emit_gamma_probe's ZERO-PAYLOAD probe, and "
             "hps_io.sv:528 writes gamma_en in the payload branch only. "
             "Stock has the same hole (video.cpp:702 early-returns with "
             "no <core>_gamma.cfg)"},
});
inline constexpr std::size_t kCoreInitStepCount = kCoreInitBase.size();

static_assert(kCoreInitBase.size() == 46, "43 class-H registers + 3 host-only rows");

[[nodiscard]] Ex<void> run_core_init(
    CoreInitContext& ctx,
    std::span<const CoreInitStep> additions = {}
);

}
