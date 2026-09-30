// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "cores/cd_engine_stats.h"
#include "cores/cd_flow_query.h"
#include "cores/cd_profile.h"
#include "cores/cd_sector_egress.h"
#include "cores/cd_tick_periods.h"
#include "cores/cd_transport.h"
#include "cores/core_window.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "svc/disc_read_service.h"

namespace mister::cores {

class CdEngine {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::size_t kMaxCommandWords = 7;

    static constexpr std::size_t kMaxStatusWords = 6;

    static constexpr std::size_t kMaxDescriptorBytes = 2048;

    virtual ~CdEngine() = default;
    CdEngine(const CdEngine&) = delete;
    CdEngine& operator=(const CdEngine&) = delete;

    Ex<void> on_command_edge(std::uint8_t req);

    Ex<void> on_tick();

    Ex<void> push_sector();

    Ex<void> reset();

    [[nodiscard]] bool cart_load_ejects() const noexcept { return do_cart_load_ejects(); }

    bool reset_pending() const noexcept { return need_reset_; }

    void request_reset() noexcept { need_reset_ = true; }

    [[nodiscard]] bool take_reset_request() noexcept {
        const bool armed = need_reset_;
        need_reset_ = false;
        return armed;
    }

    bool command_wanted() const noexcept { return command_wanted_; }
    Ex<void> accept_command(std::span<const std::uint16_t> words);

    void seed_edge_memo(std::uint8_t req) noexcept {
        last_req_ = req;
        command_wanted_ = false;
    }
    bool apply_pending_reset() noexcept;

    bool flow_bound() const noexcept { return flow_ != nullptr; }

    void set_region(bool region) noexcept { do_set_region(region); }
    Ex<void> notify_disc(bool loaded);
    bool streaming() const noexcept { return do_streaming(); }

    CdEngineStats stats() const noexcept { return do_stats(); }
    std::uint32_t crc_faults() const noexcept { return do_stats().crc_faults; }

    std::uint32_t flow_waits() const noexcept { return do_stats().flow_waits; }
    std::uint32_t cdda_sectors() const noexcept { return do_stats().cdda_sectors; }

    std::uint32_t data_sectors() const noexcept { return do_stats().data_sectors; }
    std::uint32_t idle_ticks() const noexcept { return do_stats().idle_ticks; }

    std::uint32_t gated_ticks() const noexcept { return gated_ticks_; }

    std::size_t command_word_count() const noexcept {
        const std::size_t n = do_command_word_count();
        return n < kMaxCommandWords ? n : kMaxCommandWords;
    }
    std::optional<std::uint16_t> command_selector() const noexcept { return do_command_selector(); }
    void after_command_round() noexcept { do_after_command_round(); }

    std::size_t status_words(std::uint64_t frame,
                             std::span<std::uint16_t, kMaxStatusWords> out) const noexcept {
        return do_status_words(frame, out);
    }

    struct DriveView {
        std::uint8_t state = 0;
        std::uint32_t track = 0;
        std::int32_t lba = 0;

        std::int32_t audio_lba = 0;
        bool is_data = true;
    };
    DriveView drive_view() const noexcept { return do_drive_view(); }

    [[nodiscard]] Ex<std::size_t> disc_descriptor(std::span<std::byte> out) {
        return do_disc_descriptor(out);
    }

protected:
    CdEngine(const CdTickPeriods& ticks, const os::IClock& c, ICdFlowQuery* flow,
             std::uint8_t req_seed)
        : ticks_(ticks), last_req_(req_seed), clock_(&c), flow_(flow) {}

    bool can_send_ready_(svc::TrackType type, bool skip_mode1);

private:
    virtual Ex<void> do_accept(std::span<const std::uint16_t> words) = 0;
    virtual Ex<void> do_tick() = 0;
    virtual Ex<void> do_push_sector() = 0;
    virtual Ex<void> do_notify_disc(bool loaded) = 0;
    virtual void do_reset_machine() noexcept = 0;
    virtual bool do_streaming() const noexcept = 0;
    virtual DriveView do_drive_view() const noexcept = 0;
    virtual std::size_t do_command_word_count() const noexcept = 0;
    virtual std::optional<std::uint16_t> do_command_selector() const noexcept = 0;
    virtual void do_after_command_round() noexcept = 0;
    virtual std::size_t do_status_words(
        std::uint64_t frame, std::span<std::uint16_t, kMaxStatusWords> out) const noexcept = 0;

    [[nodiscard]] virtual CdEngineStats do_stats() const noexcept = 0;

    [[nodiscard]] virtual std::uint8_t do_speed_index() const noexcept { return 0; }
    virtual void do_set_region(bool) noexcept {}

    [[nodiscard]] virtual Ex<std::size_t> do_disc_descriptor(std::span<std::byte>) {
        return std::size_t{0};
    }
    [[nodiscard]] virtual bool do_cart_load_ejects() const noexcept { return false; }

    CdTickPeriods ticks_;
    bool need_reset_ = false;
    std::uint8_t last_req_;
    bool command_wanted_ = false;
    const os::IClock* clock_;

    std::int64_t next_due_ns_ = 0;
    std::uint32_t gated_ticks_ = 0;
    ICdFlowQuery* flow_;
};

Ex<std::unique_ptr<CdEngine>> make_cd_engine(const CdProfile& profile, svc::DiscReadService& disc,
                                             const svc::DiscGeometry& geom, ICdTransport& transport,
                                             const os::IClock& clock, ICdFlowQuery* flow,
                                             ICdSectorEgress* egress = nullptr,
                                             ICoreWindow* window = nullptr);

}  // namespace mister::cores
