// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "app/core_name.h"
#include "app/mount_queue.h"
#include "cores/control_slot.h"
#include "cores/core_profile.h"
#include "cores/mount_reads.h"
#include "cores/mount_serial.h"
#include "cores/remount_asked.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "os/clock.h"
#include "os/deadline.h"
#include "svc/vfs.h"

namespace mister::app {

class ServantMounts {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct Wiring {
        const svc::Vfs& vfs;
        const os::IClock& clock;
        const cores::RemountCell& remount;
        const cores::MountReadsCell& reads;
    };

    explicit ServantMounts(const Wiring& w) noexcept
        : vfs_(w.vfs),
          clock_(w.clock),
          remount_(w.remount),
          reads_(w.reads) {}

    struct Facts {
        const cores::CoreProfile& profile;
        CoreName core;
        std::string_view shared_folder;
    };

    void step(const Facts& facts, MountQueue& mounts, MountQueue::Host& host);

    void begin_session() noexcept;

    [[nodiscard]] static std::string directory_of(
        const cores::ControlSlot& row,
        const Facts& facts
    );

    [[nodiscard]] infra::Tally refused_names() const noexcept { return refused_names_; }

private:
    bool step_control_(const Facts& facts, MountQueue& mounts, MountQueue::Host& host);
    bool step_remount_(const Facts& facts, MountQueue& mounts, MountQueue::Host& host);
    void step_repulse_(const cores::RepulseRule& rule, MountQueue& mounts, MountQueue::Host& host);

    static constexpr std::chrono::milliseconds kControlPeriod{1000};

    const svc::Vfs& vfs_;
    const os::IClock& clock_;
    const cores::RemountCell& remount_;
    const cores::MountReadsCell& reads_;

    cores::RemountCell::Reader remount_reader_{};
    cores::MountReadsCell::Reader reads_reader_{};
    std::optional<cores::RemountAsked> remount_owed_{};
    infra::Tally refused_names_{};

    os::Deadline control_due_{os::Deadline::immediate()};

    cores::MountReads seen_{};
    std::optional<os::Deadline> repulse_due_{};

    std::optional<cores::MountSerial> own_mount_{};
};

}
