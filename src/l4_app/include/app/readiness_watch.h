// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/bitstream_programmer.h"
#include "hal/pin_levels.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

class ReadinessWatch {
    TASTY_SEAT_RESIDENT(Main);

public:
    class Host {
    public:
        virtual ~Host() = default;

        [[nodiscard]] virtual bool ask_recovery_switch() = 0;

        virtual void parkless_teardown() = 0;

        virtual void fabric_suspect() noexcept = 0;

    protected:
        Host() = default;
        Host(const Host&) = default;
        Host& operator=(const Host&) = default;
    };

    struct SwitchView {
        bool standing = false;
        bool programmed = false;
    };

    struct Reboot {
        bool cold = false;
    };

    struct Tally {
        std::uint32_t recoveries = 0;
        std::uint32_t recover_polls = 0;
    };

    ReadinessWatch(const hal::PinLevelCell& levels, BitstreamProgrammer& programmer) noexcept
        : levels_(levels),
          programmer_(programmer) {}

    ReadinessWatch(const ReadinessWatch&) = delete;
    ReadinessWatch& operator=(const ReadinessWatch&) = delete;

    [[nodiscard]] std::optional<Reboot> watch_readiness(SwitchView sw, Host& host);

    void set_readiness_poll_budget(std::uint32_t n) noexcept { readiness_poll_budget_ = n; }

    [[nodiscard]] bool fabric_configured() const noexcept;
    [[nodiscard]] bool fabric_unconfigured() const noexcept;

    [[nodiscard]] bool load_ok() const noexcept { return load_ok_; }

    void latch_load_ok(bool ok) noexcept;

    [[nodiscard]] bool recovering() const noexcept { return recovering_; }

    [[nodiscard]] bool recovery_owed() const noexcept { return recover_owed_; }

    void disarm() noexcept;

    void clear_owed() noexcept;

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

    [[nodiscard]] const xthread::Telemetry<std::uint32_t, SeatTag::Main>&
    recover_polls_cell() const noexcept {
        return recover_polls_cell_;
    }

private:
    [[nodiscard]] std::optional<Reboot> recover_session_(SwitchView sw, Host& host);

    [[nodiscard]] std::optional<Reboot> probe_for_recovery_(SwitchView sw, Host& host);

    const hal::PinLevelCell& levels_;
    BitstreamProgrammer& programmer_;

    bool ready_watch_armed_ = false;
    bool recovering_ = false;
    std::uint32_t recover_tries_ = 0;
    std::uint32_t readiness_poll_budget_ = 0;
    bool recover_owed_ = false;
    bool load_ok_ = false;
    xthread::Telemetry<std::uint32_t, SeatTag::Main> recover_polls_cell_{};
    Tally tally_{};
};

}
