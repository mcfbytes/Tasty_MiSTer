// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/play_refusal.h"
#include "app/replay_play.h"
#include "app/replay_wiring.h"
#include "infra/seat.h"

namespace mister::app {

class IReplayStage {
    TASTY_SEAT_RESIDENT(Ui);

public:
    enum class Stage : std::uint8_t {
        Idle,
        Scanning,
        Hashing,
        Settling,
        Arming,
        Loading,
        Streaming,
        Stopping
    };

    struct Step {
        enum class Kind : std::uint8_t {
            Stay,
            Enter,
            Refuse,
            Done,
        };

        enum class Next : std::uint8_t { Hash, Settle, Run };
        Kind kind = Kind::Stay;
        Next next = Next::Hash;
        PlayRefusal refusal{};

        [[nodiscard]] static Step stay() noexcept { return {}; }

        [[nodiscard]] static Step enter(Next s) noexcept { return {Kind::Enter, s, {}}; }

        [[nodiscard]] static Step refuse(PlayRefusal::Why why, std::uint32_t detail = 0) noexcept {
            return {Kind::Refuse, Next::Hash, PlayRefusal{why, detail}};
        }

        [[nodiscard]] static Step done() noexcept { return {Kind::Done, Next::Hash, {}}; }
    };

    struct Ctx {
        ReplayWiring& w;
        ReplayPlay& play;

        [[nodiscard]] std::int64_t now_ns() const noexcept { return w.clock.now().count(); }
    };

    virtual ~IReplayStage() = default;

    [[nodiscard]] virtual Stage level() const noexcept = 0;

    [[nodiscard]] virtual Step enter(Ctx ctx) noexcept = 0;

    [[nodiscard]] virtual Step tick(Ctx ctx) noexcept = 0;

    [[nodiscard]] virtual Step stop(Ctx ctx) noexcept = 0;

protected:
    IReplayStage() = default;
    IReplayStage(const IReplayStage&) = default;
    IReplayStage& operator=(const IReplayStage&) = default;
};

}
