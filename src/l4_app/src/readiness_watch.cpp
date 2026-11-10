// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/readiness_watch.h"

namespace mister::app {

std::optional<ReadinessWatch::Reboot> ReadinessWatch::watch_readiness(SwitchView sw, Host& host) {
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

bool ReadinessWatch::fabric_configured() const noexcept {
    return programmer_.programmed();
}

bool ReadinessWatch::fabric_unconfigured() const noexcept {
    return programmer_.bridges_down() && !load_ok_;
}

std::optional<ReadinessWatch::Reboot> ReadinessWatch::recover_session_(SwitchView sw, Host& host) {

    host.fabric_suspect();

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

std::optional<ReadinessWatch::Reboot> ReadinessWatch::probe_for_recovery_(
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

void ReadinessWatch::disarm() noexcept {
    TASTY_SEAT_BODY(ReadinessWatch);
    ready_watch_armed_ = false;
}

void ReadinessWatch::clear_owed() noexcept {
    TASTY_SEAT_BODY(ReadinessWatch);
    recover_owed_ = false;
}

void ReadinessWatch::latch_load_ok(bool ok) noexcept {
    TASTY_SEAT_BODY(ReadinessWatch);
    load_ok_ = ok;
}

}
