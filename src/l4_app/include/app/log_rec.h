// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "app/session_state.h"
#include "app/switch_phase.h"
#include "hal/types.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/rt_stats.h"
#include "os/types.h"
#include "proto/types.h"
#include "reactor/cause_set.h"

namespace mister::app {

enum class LogSev : std::uint8_t {
    info = 0,
    warn = 1,
    alarm = 2,
};

struct LogRec {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t {
        DeadlineMiss,
        SessionTransition,
        StagingRung,
        CoreLoad,
        CoreUnload,
        Mount,
        Unmount,
        Refusal,
        RingLoss,
        DoorbellBound,
        DoorbellPolling,
        PhaseTolerated,
        ConfStrArrived,
        kCount,
    };
    static constexpr std::size_t kStore = 8;

    struct Head {
        LogSev sev = LogSev::info;
        std::uint8_t pad_[7]{};
        std::chrono::nanoseconds t{};
    };

    struct DeadlineMiss {
        static constexpr Kind kKind = Kind::DeadlineMiss;
        std::chrono::duration<std::uint32_t, std::nano> over{};
        ServiceSlot slot{};
        std::uint8_t pad_[3]{};
    };

    struct SessionTransition {
        static constexpr Kind kKind = Kind::SessionTransition;
        std::chrono::duration<std::uint32_t, std::micro> dwell{};
        SessionState from = SessionState::Boot;
        SessionState to = SessionState::Boot;
        std::uint8_t pad_[2]{};
    };

    struct StagingRung {
        static constexpr Kind kKind = Kind::StagingRung;
        std::chrono::duration<std::uint32_t, std::micro> rung{};
        std::uint16_t rung_pc = 0;
        std::uint8_t pad_[2]{};
    };

    struct CoreLoad {
        static constexpr Kind kKind = Kind::CoreLoad;
        std::chrono::duration<std::uint32_t, std::milli> load{};
    };

    struct CoreUnload {
        static constexpr Kind kKind = Kind::CoreUnload;
        std::chrono::duration<std::uint32_t, std::milli> detach{};
    };

    struct Mount {
        static constexpr Kind kKind = Kind::Mount;
        proto::SlotIndex slot{};
    };

    struct Unmount {
        static constexpr Kind kKind = Kind::Unmount;
        proto::SlotIndex slot{};
    };

    struct Refusal {
        static constexpr Kind kKind = Kind::Refusal;
        Errc code{};
        infra::ErrorSite site{};
    };

    struct RingLoss {
        static constexpr Kind kKind = Kind::RingLoss;
        std::uint32_t losses = 0;
        Kind lost = Kind::DeadlineMiss;
        std::uint8_t pad_[3]{};
    };

    struct DoorbellBound {
        static constexpr Kind kKind = Kind::DoorbellBound;
        hal::LwOffset cause_base{};
        reactor::Cause klass = reactor::Cause::None;
        os::UioLine line{};
        std::uint8_t pad_[2]{};
    };

    struct DoorbellPolling {
        static constexpr Kind kKind = Kind::DoorbellPolling;
        Errc why{};
        reactor::Cause klass = reactor::Cause::None;
        os::UioLine line{};
    };

    struct PhaseTolerated {
        static constexpr Kind kKind = Kind::PhaseTolerated;
        proto::BindGeneration gen{};
        app::phase::PhaseKind phase = app::phase::PhaseKind::Idle;
        app::phase::PhaseKind under = app::phase::PhaseKind::Idle;
        app::phase::Stimulus stimulus = app::phase::Stimulus::LoadCore;
        std::uint8_t pad_[3]{};
    };

    struct ConfStrArrived {
        static constexpr Kind kKind = Kind::ConfStrArrived;
        proto::BindGeneration bind_gen{};
        app::phase::PhaseKind phase = app::phase::PhaseKind::Idle;
        app::phase::PhaseKind under = app::phase::PhaseKind::Idle;
        bool awaited = false;
        std::uint8_t pad_[3]{};
    };

    using Alternatives = std::tuple<
        DeadlineMiss,
        SessionTransition,
        StagingRung,
        CoreLoad,
        CoreUnload,
        Mount,
        Unmount,
        Refusal,
        RingLoss,
        DoorbellBound,
        DoorbellPolling,
        PhaseTolerated,
        ConfStrArrived>;

    Kind kind = Kind::DeadlineMiss;
    std::uint8_t pad_[7]{};
    Head head{};
    std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<LogRec> && infra::alternatives_are_total<LogRec>());
static_assert(sizeof(LogRec) == 32 && alignof(LogRec) == 8);

}
