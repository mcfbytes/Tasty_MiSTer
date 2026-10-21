// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/rt_stats.h"
#include "infra/seat.h"
#include "reactor/executive.h"
#include "os/uio_handle.h"
#include "reactor/notifier.h"

namespace mister::reactor {

class CoreNotifiers {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit CoreNotifiers(Executive& exec) noexcept : exec_(exec) {}

    ~CoreNotifiers() { release(); }

    CoreNotifiers(const CoreNotifiers&) = delete;
    CoreNotifiers& operator=(const CoreNotifiers&) = delete;

    [[nodiscard]] Ex<NotifierSlot> bind_doorbell(
        os::UioLineRef uio,
        Cause klass,
        infra::OptRef<const hal::RegisterWindow<hal::CauseReg>> window
    );

    void release() noexcept;

    void retry_release() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return live_; }

    [[nodiscard]] std::uint32_t release_failures() const noexcept { return release_failures_; }

private:
    struct Entry {
        NotifierSlot slot{};
        bool live = false;
        bool refused = false;
    };

    void retire_(bool refused_only) noexcept;

    Executive& exec_;
    Entry entries_[kMaxLines];
    std::size_t live_ = 0;
    std::uint32_t release_failures_ = 0;
};

}
