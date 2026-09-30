// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/seat.h"

namespace mister {

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
        kCount,
    };
    static constexpr std::size_t kStore = 8;

    struct Head {
        LogSev sev = LogSev::info;
        std::uint8_t pad_[7]{};
        std::uint64_t t_ns = 0;
    };

    struct DeadlineMiss {
        static constexpr Kind kKind = Kind::DeadlineMiss;
        std::uint32_t over_ns = 0;
        std::uint16_t slot = 0;
        std::uint8_t pad_[2]{};
    };
    struct SessionTransition {
        static constexpr Kind kKind = Kind::SessionTransition;
        std::uint32_t dwell_us = 0;
        std::uint8_t from = 0;
        std::uint8_t to = 0;
        std::uint8_t pad_[2]{};
    };
    struct StagingRung {
        static constexpr Kind kKind = Kind::StagingRung;
        std::uint32_t rung_us = 0;
        std::uint16_t rung_pc = 0;
        std::uint8_t pad_[2]{};
    };
    struct CoreLoad {
        static constexpr Kind kKind = Kind::CoreLoad;
        std::uint32_t load_ms = 0;
    };
    struct CoreUnload {
        static constexpr Kind kKind = Kind::CoreUnload;
        std::uint32_t detach_ms = 0;
    };
    struct Mount {
        static constexpr Kind kKind = Kind::Mount;
        std::uint8_t slot = 0;
    };
    struct Unmount {
        static constexpr Kind kKind = Kind::Unmount;
        std::uint8_t slot = 0;
    };
    struct Refusal {
        static constexpr Kind kKind = Kind::Refusal;
        Errc code{};
        std::uint16_t site = 0;
    };
    struct RingLoss {
        static constexpr Kind kKind = Kind::RingLoss;
        std::uint32_t losses = 0;
        Kind lost = Kind::DeadlineMiss;
        std::uint8_t pad_[3]{};
    };
    struct DoorbellBound {
        static constexpr Kind kKind = Kind::DoorbellBound;
        std::uint32_t cause_base = 0;
        std::uint8_t klass = 0;
        std::uint8_t line = 0;
        std::uint8_t pad_[2]{};
    };
    struct DoorbellPolling {
        static constexpr Kind kKind = Kind::DoorbellPolling;
        Errc why{};
        std::uint8_t klass = 0;
        std::uint8_t line = 0;
    };

    using Alternatives =
        std::tuple<DeadlineMiss, SessionTransition, StagingRung, CoreLoad, CoreUnload, Mount,
                   Unmount, Refusal, RingLoss, DoorbellBound, DoorbellPolling>;

    Kind kind = Kind::DeadlineMiss;
    std::uint8_t pad_[7]{};
    Head head{};
    std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<LogRec> && infra::alternatives_are_total<LogRec>());
static_assert(sizeof(LogRec) == 32 && alignof(LogRec) == 8);

}  // namespace mister
