// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/link_op.h"

namespace mister::svc {
class IStorageLifecycle;
}

namespace mister::app {

class FencedCoreSignals;
class NvramUpload;
class WindowJobService;

class RebootAnswer {
    TASTY_SEAT_RESIDENT(RT);

public:
    class Host {
    public:
        [[nodiscard]] virtual bool reboot_push_quiesced(std::uint16_t seq) noexcept = 0;

    protected:
        Host() = default;
        Host(const Host&) = default;
        Host& operator=(const Host&) = default;
        ~Host() = default;
    };

    struct Wiring {
        FencedCoreSignals& signals;
        const NvramUpload& nvram;
        infra::OptRef<WindowJobService> window_jobs;
        infra::OptRef<svc::IStorageLifecycle> storage_life;
    };

    enum class After : std::uint8_t { EndStartAndFail, CountStale, ReleaseBus };

    explicit RebootAnswer(const Wiring& w) noexcept
        : signals_(w.signals),
          nvram_(w.nvram),
          window_jobs_(w.window_jobs),
          storage_life_(w.storage_life) {}

    [[nodiscard]] After on_reboot_now(const proto::LinkOp::RebootNow& a);

    void answer_reboot(Host& host) noexcept;

    [[nodiscard]] bool rebooting() const noexcept { return rebooting_; }

private:
    [[nodiscard]] After perform_reboot_() const noexcept;
    [[nodiscard]] bool io_settled_() const noexcept;

    FencedCoreSignals& signals_;
    const NvramUpload& nvram_;
    infra::OptRef<WindowJobService> window_jobs_{};
    infra::OptRef<svc::IStorageLifecycle> storage_life_{};
    bool rebooting_ = false;
    bool reboot_answered_ = false;
    std::uint16_t reboot_seq_ = 0;
};

}
