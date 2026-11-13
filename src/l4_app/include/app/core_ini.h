// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "app/board_ops.h"
#include "app/ini_cell.h"
#include "app/ini_parse.h"
#include "app/launcher_profile.h"
#include "app/mra_facts.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::svc {
class Vfs;
}

namespace mister::hal {
class IBootHandoff;
}

namespace mister::app {

class CoreIni final {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct Resolved {
        svc::ConfigSnapshot cfg{};
        bool waitmount_exhausted = false;
        infra::OptRef<const LauncherProfile> launcher{};
    };

    CoreIni(
        ConfigCell& cell,
        IniCell& ini_cell,
        const svc::Vfs& vfs,
        infra::OptRef<ISleeper> sleeper,
        infra::OptRef<hal::IBootHandoff> handoff
    ) noexcept
        : cell_(cell),
          ini_cell_(ini_cell),
          vfs_(vfs),
          sleeper_(sleeper),
          handoff_(handoff) {}

    CoreIni(const CoreIni&) = delete;
    CoreIni& operator=(const CoreIni&) = delete;

    void arm_replay(bool strict) noexcept;

    [[nodiscard]] Resolved resolve(std::string_view conf_name, const MraFacts& facts);

    void publish(const svc::ConfigSnapshot& cfg);

    [[nodiscard]] ConfigCell& cell() noexcept { return cell_; }

    [[nodiscard]] svc::IniCatalog catalog() const noexcept { return ini_cell_.sample().value; }

    [[nodiscard]] bool boot_parsed() const noexcept { return !boot_pending_; }

    [[nodiscard]] const std::string& boot_handoff() const noexcept { return boot_handoff_; }

    [[nodiscard]] const xthread::Telemetry<std::uint8_t, SeatTag::Main>&
    direct_video_cell() const noexcept {
        return dv_cell_;
    }

    [[nodiscard]] const IdleKnobsCell& idle_cell() const noexcept { return idle_cell_; }

private:
    [[nodiscard]] bool wait_for_mount_(std::string_view needle);

    ConfigCell& cell_;
    IniCell& ini_cell_;
    const svc::Vfs& vfs_;
    infra::OptRef<ISleeper> sleeper_;
    infra::OptRef<hal::IBootHandoff> handoff_;
    bool replay_ = false;
    bool strict_ = false;
    xthread::Telemetry<std::uint8_t, SeatTag::Main> dv_cell_{};
    IdleKnobsCell idle_cell_{};
    bool boot_pending_ = true;
    std::string boot_handoff_;
};

}
