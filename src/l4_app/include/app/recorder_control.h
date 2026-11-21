// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

#include "app/capture/rec_control.h"
#include "app/capture/rec_write_status.h"
#include "app/capture/recorder_status.h"
#include "infra/xthread/coworker.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"

namespace mister::infra::xthread {
class DiagLog;
}

namespace mister::app {

class IdentityLatch;

class RecorderControl : public xthread::ICoworker {
    TASTY_SEAT_RESIDENT(Ui);

public:
    using Chan = capture::RecControlChannel;

    static constexpr std::string_view kRoot = "/media/";

    struct Wiring {
        infra::OptRef<Chan> control{};
        infra::OptRef<const capture::RecorderStatusCell> status{};
        infra::OptRef<const capture::RecWriteStatusCell> writer{};
        infra::OptRef<xthread::DiagLog> diag{};
        infra::OptRef<const IdentityLatch> identity{};
        std::string_view root = kRoot;
    };

    explicit RecorderControl(const Wiring& w) noexcept
        : w_(w),
          requests_(w_.control ? w_.control->owner() : Chan::Owner{}) {}

    RecorderControl(const RecorderControl&) = delete;
    RecorderControl& operator=(const RecorderControl&) = delete;

    [[nodiscard]] bool take_start(
        std::string_view path,
        capture::RecMode mode,
        capture::RecOptions opt = {}
    ) noexcept;
    [[nodiscard]] bool take_arm(
        std::string_view path,
        capture::RecMode mode,
        capture::RecOptions opt = {}
    ) noexcept;
    [[nodiscard]] bool take_stop() noexcept;
    [[nodiscard]] bool take_disarm() noexcept;

    [[nodiscard]] bool can_record(std::string_view path) const;

    void serve() noexcept override;

    [[nodiscard]] static capture::RecPath resolve(
        std::string_view arg,
        std::string_view root,
        std::string_view name,
        std::time_t now
    ) noexcept;

    [[nodiscard]] ControlGeneration generation() const noexcept { return gen_; }

    [[nodiscard]] capture::RecVerdict last_verdict() const noexcept { return last_verdict_; }

private:
    [[nodiscard]] std::string name_() const;
    [[nodiscard]] bool request_(
        capture::RecOp op,
        std::string_view path,
        capture::RecMode mode,
        capture::RecOptions opt
    ) noexcept;
    [[nodiscard]] bool publish_(
        capture::RecOp op,
        std::string_view path,
        capture::RecMode mode,
        capture::RecOptions opt
    ) noexcept;

    void reap_() noexcept;

    Wiring w_;
    Chan::Owner requests_{};
    ControlGeneration gen_{};
    capture::RecorderStatusCell::Reader status_seen_{};
    ControlGeneration logged_end_{};
    capture::RecVerdict last_verdict_ = capture::RecVerdict::None;
};

}
