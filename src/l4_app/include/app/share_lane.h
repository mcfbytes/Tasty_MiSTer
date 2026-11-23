// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>

#include "app/load_window_map.h"
#include "app/share_grant.h"
#include "hal/fpga_aperture.h"
#include "hal/fpga_memory.h"
#include "infra/park_timeout.h"
#include "infra/seat.h"
#include "infra/seat_owner_io.h"
#include "infra/tally.h"
#include "svc/io_coworker.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class ShareLane final : public svc::IIoCoworker {
    TASTY_SEAT_RESIDENT(Io);

public:
    struct Wiring {
        const ShareGrantCell& grant;
        const svc::Vfs& vfs;
        ILoadWindowMap<seat_owner::Io>& map;
        hal::FpgaAperture aperture{};
    };

    struct Tally {
        infra::Tally opened{};
        infra::Tally refused{};
        infra::Tally closed{};
        infra::Tally resets{};
        infra::Tally served{};
    };

    explicit ShareLane(const Wiring& w) noexcept : w_(w) {}

    void serve() noexcept override;

    [[nodiscard]] bool idle() const noexcept override {
        TASTY_SEAT_BODY(ShareLane);
        return true;
    }

    [[nodiscard]] infra::ParkTimeout poll_period() const noexcept override;

    [[nodiscard]] bool bound() const noexcept { return host_ != nullptr; }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    void apply_(const ShareGrant& g) noexcept;
    void open_(const ShareGrant::Open& o) noexcept;
    void close_() noexcept;

    Wiring w_;
    ShareGrantCell::Reader reader_{};
    ShareGrant incoming_{};
    std::optional<ShareGrant::Open> applied_{};
    std::unique_ptr<cores::IShareHost> host_{};
    std::optional<hal::FpgaMemory> page_{};
    infra::ParkTimeout poll_{infra::kForever};
    Tally tally_{};
};

}
