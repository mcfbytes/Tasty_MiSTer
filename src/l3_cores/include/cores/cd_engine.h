// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "cores/cd_cadence_gate.h"
#include "cores/cd_drive_state.h"
#include "cores/cd_link.h"
#include "cores/cd_engine_stats.h"
#include "cores/cd_profile.h"
#include "cores/cd_tick_periods.h"
#include "cores/core_window.h"
#include "cores/types.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "svc/disc_read_service.h"
#include "svc/types.h"

namespace mister::cores {

struct CdDialectPorts;

class CdEngine {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::size_t kMaxCommandWords = 7;

    static constexpr std::size_t kMaxStatusWords = 6;

    static constexpr std::size_t kMaxDescriptorBytes = 2048;

    virtual ~CdEngine() = default;
    CdEngine(const CdEngine&) = delete;
    CdEngine& operator=(const CdEngine&) = delete;

    [[nodiscard]] bool on_command_edge(std::uint8_t req) noexcept;

    [[nodiscard]] Ex<void> on_tick(CdDialectPorts& p);

    [[nodiscard]] Ex<void> push_sector(CdDialectPorts& p);

    void reset() noexcept;

    [[nodiscard]] bool cart_load_ejects() const noexcept { return do_cart_load_ejects(); }

    bool reset_pending() const noexcept { return need_reset_; }

    void request_reset() noexcept { need_reset_ = true; }

    [[nodiscard]] bool take_reset_request() noexcept {
        const bool armed = need_reset_;
        need_reset_ = false;
        return armed;
    }

    bool command_wanted() const noexcept { return command_wanted_; }

    [[nodiscard]] Ex<void> accept_command(std::span<const std::uint16_t> words, CdDialectPorts& p);

    void seed_edge_memo(std::uint8_t req) noexcept {
        last_req_ = req;
        command_wanted_ = false;
    }

    bool apply_pending_reset() noexcept;

    void set_region(bool region) noexcept { do_set_region(region); }

    void notify_disc(bool loaded, const svc::DiscGeometry& geom) noexcept;

    bool streaming(const svc::DiscGeometry& geom) const noexcept { return do_streaming(geom); }

    CdEngineStats stats() const noexcept { return do_stats(); }

    std::uint32_t crc_faults() const noexcept { return do_stats().crc_faults; }

    std::uint32_t flow_waits() const noexcept { return do_stats().flow_waits; }

    std::uint32_t cdda_sectors() const noexcept { return do_stats().cdda_sectors; }

    std::uint32_t data_sectors() const noexcept { return do_stats().data_sectors; }

    std::uint32_t idle_ticks() const noexcept { return do_stats().idle_ticks; }

    std::uint32_t gated_ticks() const noexcept { return cadence_.gated_ticks(); }

    std::size_t command_word_count() const noexcept {
        const std::size_t n = do_command_word_count();
        return n < kMaxCommandWords ? n : kMaxCommandWords;
    }

    std::optional<cdwire::Selector> command_selector() const noexcept {
        return do_command_selector();
    }

    void after_command_round() noexcept { do_after_command_round(); }

    struct DriveView {
        CdDriveState state = CdDriveState::NoDisc;
        svc::TrackIndex track{};
        HeadLba lba{};

        HeadLba audio_lba{};
        bool is_data = true;
    };

    DriveView drive_view(const svc::DiscGeometry& geom) const noexcept {
        return do_drive_view(geom);
    }

    [[nodiscard]] Ex<std::span<const std::byte>> disc_descriptor(
        std::span<std::byte, kMaxDescriptorBytes> out,
        const svc::DiscGeometry& geom
    ) {
        return do_disc_descriptor(out, geom);
    }

protected:
    CdEngine(const CdTickPeriods& ticks, const os::IClock& c, std::uint8_t req_seed)
        : cadence_(ticks, c),
          last_req_(req_seed) {}

    template <std::size_t N>
    [[nodiscard]] Ex<void> send_status_words_(
        const std::array<std::uint16_t, N>& words,
        ICdLink& link
    ) {
        static_assert(N <= kMaxStatusWords, "a status frame is at most kMaxStatusWords");
        return link.send_status(words);
    }

private:
    [[nodiscard]] virtual Ex<void> do_accept(
        std::span<const std::uint16_t> words,
        CdDialectPorts& p
    ) = 0;
    [[nodiscard]] virtual Ex<void> do_tick(CdDialectPorts& p) = 0;
    [[nodiscard]] virtual Ex<void> do_push_sector(CdDialectPorts& p) = 0;
    virtual void do_notify_disc(bool loaded, const svc::DiscGeometry& geom) noexcept = 0;
    virtual void do_reset_machine() noexcept = 0;
    virtual bool do_streaming(const svc::DiscGeometry& geom) const noexcept = 0;
    virtual DriveView do_drive_view(const svc::DiscGeometry& geom) const noexcept = 0;
    virtual std::size_t do_command_word_count() const noexcept = 0;
    virtual std::optional<cdwire::Selector> do_command_selector() const noexcept = 0;
    virtual void do_after_command_round() noexcept = 0;

    [[nodiscard]] virtual CdEngineStats do_stats() const noexcept = 0;

    [[nodiscard]] virtual CdSpeedIndex do_speed_index() const noexcept { return CdSpeedIndex{}; }

    virtual void do_set_region(bool) noexcept {}

    [[nodiscard]] virtual Ex<std::span<const std::byte>> do_disc_descriptor(
        std::span<std::byte, kMaxDescriptorBytes>,
        const svc::DiscGeometry&
    ) {
        return std::span<const std::byte>{};
    }

    [[nodiscard]] virtual bool do_cart_load_ejects() const noexcept { return false; }

    CdCadenceGate cadence_;
    bool need_reset_ = false;
    std::uint8_t last_req_;
    bool command_wanted_ = false;
};

std::unique_ptr<CdEngine> make_cd_engine(
    const CdProfile& profile,
    svc::DiscReadService& disc,
    const os::IClock& clock,
    infra::OptRef<ICoreWindow> window = {}
);

}
