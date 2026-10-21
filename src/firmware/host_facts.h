// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "os/kernel_rt.h"
#include "hal/thread_map.h"
#include "ui/host_sampler.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::os {
class IClock;
}

namespace mister::svc {
class Vfs;
}

namespace mister::ui {
class MenuModel;
struct StatusBarFacts;
struct SystemFacts;
}

namespace mister::fw {

class HostFacts : public ui::IHostSampler {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr SeatTag kSeat = SeatTag::Ui;

    HostFacts(
        const svc::Vfs& vfs,
        const os::IClock& clock,
        const ui::MenuModel& menu,
        os::KernelRt kernel_rt
    ) noexcept
        : vfs_(vfs),
          clock_(clock),
          menu_(menu),
          kernel_rt_(kernel_rt) {}

    HostFacts(const HostFacts&) = delete;
    HostFacts& operator=(const HostFacts&) = delete;

    void sample(ui::SystemFacts& sys, ui::StatusBarFacts& bar) override;

private:
    const svc::Vfs& vfs_;
    bool splash_spent_ = false;
    const os::IClock& clock_;

    const ui::MenuModel& menu_;
    std::chrono::nanoseconds bt_probed_at_{};
    bool bt_probed_ = false;
    bool bt_up_ = false;

    std::chrono::nanoseconds net_probed_at_{};
    bool net_probed_ = false;
    bool net_wired_ = false;
    bool net_wifi_ = false;

    os::KernelRt kernel_rt_ = os::KernelRt::Unknown;
};

}
