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

inline std::optional<ReadinessWatch::Reboot> ReadinessWatch::watch_readiness(
    SwitchView sw,
    Host& host
) {
    TASTY_SEAT_BODY(ReadinessWatch);

    if (!recovering_ && fabric_unconfigured()) {
        recover_owed_ = false;
        return std::nullopt;
    }
    if (recover_owed_) {
        if (!sw.standing)
            return recover_session_(sw, host);
        return std::nullopt;
    }
    if (recovering_)
        return probe_for_recovery_(sw, host);
    hal::PinLevels lv{};

    if (levels_.sample_into(lv) == 0u)
        return std::nullopt;
    if (lv.core_ready) {
        ready_watch_armed_ = true;
        return std::nullopt;
    }
    if (!ready_watch_armed_)
        return std::nullopt;
    ready_watch_armed_ = false;
    return recover_session_(sw, host);
}

inline bool ReadinessWatch::fabric_configured() const noexcept {
    return programmer_.programmed();
}

inline bool ReadinessWatch::fabric_unconfigured() const noexcept {
    return programmer_.bridges_down() && !load_ok_;
}

inline std::optional<ReadinessWatch::Reboot> ReadinessWatch::recover_session_(
    SwitchView sw,
    Host& host
) {

    if (sw.standing) {
        recover_owed_ = !sw.programmed;
        return std::nullopt;
    }
    recover_owed_ = false;
    ++tally_.recoveries;

    sw.standing = host.ask_recovery_switch();
    if (!sw.standing)
        host.parkless_teardown();
    recovering_ = true;
    recover_tries_ = 0;
    return probe_for_recovery_(sw, host);
}

inline std::optional<ReadinessWatch::Reboot> ReadinessWatch::probe_for_recovery_(
    SwitchView sw,
    Host& host
) {
    if (fabric_configured()) {

        recovering_ = false;
        if (!sw.standing)
            (void)host.ask_recovery_switch();
        return std::nullopt;
    }
    recover_polls_cell_.publish(++tally_.recover_polls);
    ++recover_tries_;
    if (readiness_poll_budget_ == 0 || recover_tries_ < readiness_poll_budget_) {
        return std::nullopt;
    }
    recovering_ = false;

    return Reboot{.cold = fabric_unconfigured()};
}

inline void ReadinessWatch::disarm() noexcept {
    TASTY_SEAT_BODY(ReadinessWatch);
    ready_watch_armed_ = false;
}

inline void ReadinessWatch::clear_owed() noexcept {
    TASTY_SEAT_BODY(ReadinessWatch);
    recover_owed_ = false;
}

inline void ReadinessWatch::latch_load_ok(bool ok) noexcept {
    TASTY_SEAT_BODY(ReadinessWatch);
    load_ok_ = ok;
}

}
