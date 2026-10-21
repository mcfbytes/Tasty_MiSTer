// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <ctime>
#include <string_view>

#include "app/rec_control.h"
#include "app/rec_write_status.h"
#include "app/recorder_status.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::xthread {
class DiagLog;
}

namespace mister::app {

class IdentityLatch;

class RecorderControl {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::string_view kRoot = "/media/";

    struct Wiring {
        infra::OptRef<RecControlCell> control{};
        infra::OptRef<xthread::WakeFlag> capture_wake{};
        infra::OptRef<const RecorderStatusCell> status{};
        infra::OptRef<const RecWriteStatusCell> writer{};
        infra::OptRef<xthread::DiagLog> diag{};
        infra::OptRef<const IdentityLatch> identity{};
        std::string_view root = kRoot;
    };

    explicit RecorderControl(const Wiring& w) noexcept : w_(w) {}

    RecorderControl(const RecorderControl&) = delete;
    RecorderControl& operator=(const RecorderControl&) = delete;

    [[nodiscard]] bool take_start(
        std::string_view path,
        RecMode mode,
        RecOptions opt = {}
    ) noexcept;
    [[nodiscard]] bool take_arm(std::string_view path, RecMode mode, RecOptions opt = {}) noexcept;
    [[nodiscard]] bool take_stop() noexcept;
    [[nodiscard]] bool take_disarm() noexcept;

    [[nodiscard]] bool can_record(std::string_view path) const;

    void tick() noexcept;

    [[nodiscard]] static RecPath resolve(
        std::string_view arg,
        std::string_view root,
        std::string_view name,
        std::time_t now
    ) noexcept;

    [[nodiscard]] std::uint16_t generation() const noexcept { return gen_; }

private:
    [[nodiscard]] std::string name_() const;
    [[nodiscard]] bool request_(
        RecOp op,
        std::string_view path,
        RecMode mode,
        RecOptions opt
    ) noexcept;
    [[nodiscard]] bool publish_(
        RecOp op,
        std::string_view path,
        RecMode mode,
        RecOptions opt
    ) noexcept;

    Wiring w_;
    std::uint16_t gen_ = 0;
    RecorderStatusCell::Reader status_seen_{};
    std::uint16_t logged_answer_ = 0;
    std::uint16_t logged_end_ = 0;
};

}
