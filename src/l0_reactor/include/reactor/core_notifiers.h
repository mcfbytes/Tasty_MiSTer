// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>

#include "infra/error.h"
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
        os::UioHandle uio, Cause klass, std::optional<hal::RegisterWindow<hal::CauseReg>> window);

    void release() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return live_; }

private:
    struct Entry {
        std::optional<hal::RegisterWindow<hal::CauseReg>> window;
        NotifierSlot slot{};
        bool live = false;
    };

    Executive& exec_;
    Entry entries_[kMaxLines];
    std::size_t live_ = 0;
};

}  // namespace mister::reactor
