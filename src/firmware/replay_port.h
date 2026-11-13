// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>

#include "app/replay_feeder.h"
#include "app/types.h"
#include "infra/seat.h"
#include "tasty_refusal.h"

namespace mister::fw {

class IReplayPort {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct SettingNo {
        std::size_t v = 0;

        [[nodiscard]] constexpr SettingNo next() const noexcept { return SettingNo{v + 1}; }

        [[nodiscard]] constexpr bool reached(SettingNo last) const noexcept { return v >= last.v; }

        template <class Row>
        [[nodiscard]] constexpr const Row* row_in(std::span<const Row> rows) const noexcept {
            return v < rows.size() ? &rows[v] : nullptr;
        }

        friend constexpr bool operator==(SettingNo, SettingNo) = default;
    };

    virtual ~IReplayPort() = default;

    [[nodiscard]] virtual bool take_play(const app::ReplayFeeder::Play& play) noexcept = 0;
    virtual void take_stop() noexcept = 0;

    [[nodiscard]] virtual app::FrameCount movie_frames() const noexcept = 0;

    [[nodiscard]] virtual bool active() const noexcept = 0;

    [[nodiscard]] virtual std::optional<ReplayRefusal> refusal() const noexcept = 0;

    [[nodiscard]] virtual SettingNo settings_set() const noexcept = 0;
    [[nodiscard]] virtual std::string setting_line(SettingNo i) const = 0;

protected:
    IReplayPort() = default;
    IReplayPort(const IReplayPort&) = default;
    IReplayPort& operator=(const IReplayPort&) = default;
};

}
