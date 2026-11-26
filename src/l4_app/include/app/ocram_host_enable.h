// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

#include "app/bay_counts.h"
#include "app/program_guard.h"
#include "app/types.h"
#include "hal/ocram.h"
#include "hal/ocram_platform.h"
#include "hal/ocram_presence.h"
#include "hal/phys_region.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "os/delay.h"
#include "os/nanosleep_delay.h"
#include "proto/types.h"

namespace mister::app {

class OcramHostEnable final : public IProgramGuard {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr std::chrono::microseconds kArmBound{50'000};
    static constexpr std::chrono::microseconds kEchoBound{1'000};

    static constexpr std::chrono::microseconds kStopBound{1'000};
    static constexpr std::chrono::microseconds kPollGap{100};

    struct Wiring {
        hal::PhysRegion window{};
        infra::OptRef<hal::IOcramPlatform> platform{};

        infra::OptRef<hal::ocram::Window> mapped{};
        infra::OptRef<os::IDelay> delay{};
    };

    struct MountRings {
        hal::ocram::RingDesc ring{};
        std::array<std::optional<hal::ocram::SectorShape>, hal::ocram::kMountSlots.size()> slots{};
    };

    struct SectorRings {
        hal::ocram::RingDesc ring{};
        hal::ocram::RingDesc evt{};
        hal::ocram::SectorShape shape;
        std::optional<MountRings> mounts{};
        bool bind_records = false;
        bool events = false;
    };

    struct Session {
        std::optional<hal::ocram::RingDesc> ring{};
        hal::ocram::Nonce nonce{};
        hal::ocram::ImageRows rows{};
        hal::ocram::ImageRowBytes row_bytes{};
        std::optional<SectorRings> sector{};

        std::optional<hal::ocram::RingDesc> lane{};
        std::optional<hal::ocram::LaneShape> lane_shape{};
    };

    OcramHostEnable(
        BayCounts& counts,
        BayCountsCell& cell,
        const os::IClock& clock,
        Wiring wiring
    ) noexcept;
    OcramHostEnable(const OcramHostEnable&) = delete;
    OcramHostEnable& operator=(const OcramHostEnable&) = delete;

    [[nodiscard]] std::optional<hal::ocram::Nonce> prepare(
        hal::OcramPresence presence,
        FabricGeneration fabric
    ) noexcept;

    void raised() noexcept;

    [[nodiscard]] bool handshake(hal::ocram::Nonce nonce) noexcept;

    [[nodiscard]] std::optional<Session> read_session() noexcept;

    [[nodiscard]] bool stop_after_drop() noexcept;

    [[nodiscard]] std::optional<hal::ocram::RingLend> lend_osd() const noexcept;
    void bind_osd() noexcept;

    void bind_events() noexcept;

    [[nodiscard]] bool has_session() const noexcept { return session_.has_value(); }

    [[nodiscard]] bool has_osd() const noexcept { return session_ && session_->ring.has_value(); }

    struct SectorLends {
        hal::ocram::RingLend ring{};
        hal::ocram::RingLend evt{};
        hal::ocram::RingLend mount{};
    };

    [[nodiscard]] std::optional<SectorRings> sector_rings() const noexcept;
    [[nodiscard]] std::optional<SectorLends> lend_sector() const noexcept;

    [[nodiscard]] std::optional<proto::OsdRowCount> osd_rows() const noexcept;

    [[nodiscard]] std::optional<hal::ocram::RingLend> lend_lane() const noexcept;

    [[nodiscard]] std::optional<hal::ocram::RingDesc> lane_ring() const noexcept {
        return session_ ? session_->lane : std::nullopt;
    }

    [[nodiscard]] std::optional<hal::ocram::LaneShape> lane_shape() const noexcept {
        return session_ ? session_->lane_shape : std::nullopt;
    }

    [[nodiscard]] bool session_lost() noexcept;

    void proof() override;

    void drop() noexcept;

private:
    [[nodiscard]] bool stop_proof_(std::chrono::microseconds bound) noexcept;

    [[nodiscard]] static const char* osd_rings_(
        const hal::ocram::ControlPage& page,
        Session& got
    ) noexcept;
    [[nodiscard]] static const char* sector_rings_(
        const hal::ocram::ControlPage& page,
        Session& got
    ) noexcept;
    [[nodiscard]] static const char* lane_rings_(
        const hal::ocram::ControlPage& page,
        Session& got
    ) noexcept;

    [[nodiscard]] static const char* mount_rings_(
        const hal::ocram::ControlPage& page,
        SectorRings& got
    ) noexcept;
    [[nodiscard]] bool poll_hello(
        hal::ocram::HelloState want,
        std::chrono::microseconds bound
    ) noexcept;
    [[nodiscard]] std::chrono::microseconds since_(
        os::RawMonotonicClock::time_point start
    ) const noexcept;
    void latch_() noexcept;
    void publish_() noexcept;

    [[nodiscard]] os::IDelay& delay_ref_() noexcept { return delay_ ? *delay_ : own_delay_; }

    BayCounts& counts_;
    BayCountsCell& cell_;
    const os::IClock& clock_;
    os::NanosleepDelay own_delay_{};
    infra::OptRef<os::IDelay> delay_{};
    hal::PhysRegion window_{};
    infra::OptRef<hal::IOcramPlatform> platform_{};
    infra::OptRef<hal::ocram::Window> prestaged_{};
    std::optional<hal::ocram::Window> mapped_{};
    std::optional<Session> session_{};
    FabricGeneration fabric_{};
    FabricGeneration latched_at_{};
    hal::ocram::Nonce nonce_{};
    bool run_edge_ = false;
    bool no_edge_ = false;
    bool bound_seen_ = false;
    bool lost_ = false;
};

}
