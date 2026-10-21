// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "app/activity_source.h"
#include "app/adv7513_seat.h"
#include "infra/opt_ref.h"
#include "app/event.h"
#include "app/idle_blank.h"
#include "app/output_geometry.h"
#include "app/video_geometry_record.h"
#include "app/video_ini.h"
#include "app/video_pump_stats.h"
#include "app/video_wire.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "os/clock.h"
#include "hal/hdmi_int.h"
#include "hal/video_out_decl.h"
#include "svc/adv7513_probe_report.h"
#include "svc/i2c_adapter.h"
#include "svc/pll_params.h"
#include "svc/stored_mode.h"
#include "svc/video_service.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

using VideoStatsCell = xthread::Telemetry<VideoPumpStats, SeatTag::Ui>;

class VideoPump : public IOutputGeometry {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::size_t kSpecMax = 1024;

    struct Wiring {
        const IActivitySource& activity;
        LinkTxChannel<SeatTag::Ui>& link_tx;
    };

    VideoPump(
        const svc::Vfs& vfs,
        const os::IClock& clock,
        const hal::VideoOutDecl& board,
        infra::OptRef<svc::adv7513::II2cAdapter> i2c,
        const hal::IHdmiInterrupt& hdmi_int,
        Wiring wiring
    ) noexcept;

    VideoPump(const VideoPump&) = delete;
    VideoPump& operator=(const VideoPump&) = delete;

    [[nodiscard]] bool take_video_mode(std::string_view spec) noexcept;

    [[nodiscard]] bool take_fb_cmd(std::string_view line) noexcept;

    void tick();

    void on_pause() noexcept { i2c_held_ = true; }

    [[nodiscard]] bool i2c_held() const noexcept { return i2c_held_; }

    void on_core_loaded(bool front_end);
    void on_session_over();

    VideoWire& wire() noexcept { return wire_; }

    using GeometryRecord = VideoGeometryRecord;

    [[nodiscard]] GeometryRecord geometry_record() const noexcept {
        return geo_cell_.sample().value;
    }

    [[nodiscard]] const VideoGeometryCell& geometry_cell() const noexcept { return geo_cell_; }

    void resample_geometry() noexcept { video_->request_force_sample(); }

    std::uint32_t geometry_polls() const noexcept { return geo_polls_; }

    std::uint32_t geometry_edges() const noexcept { return geo_edges_; }

    void publish_scaling_policy() noexcept;

    void set_vscale(std::uint8_t mode, std::uint8_t border) noexcept {
        vscale_mode_ = mode;
        vscale_border_ = border;
        publish_scaling_policy();
    }

    void set_support_fhd(bool v) noexcept { support_fhd_ = v; }

    void force_vsync_adjust(std::uint8_t v) noexcept;

    void arm_replay_ini(bool strict) noexcept;

    [[nodiscard]] std::uint8_t vsync_adjust() const noexcept { return ini_.vsync_adjust; }

    [[nodiscard]] bool direct_video() const noexcept { return ini_.direct_video; }

    [[nodiscard]] std::uint8_t direct_video_ini() const noexcept { return ini_.direct_video_ini; }

    [[nodiscard]] bool ini_read() const noexcept { return ini_read_; }

    [[nodiscard]] std::uint16_t output_width() const noexcept override {
        TASTY_SEAT_BODY(VideoPump);
        return scrw_;
    }

    [[nodiscard]] std::uint16_t output_height() const noexcept override {
        TASTY_SEAT_BODY(VideoPump);
        return scrh_;
    }

    void set_core_name(std::string_view name) noexcept;

    void set_mode_override(std::string_view spec) noexcept;

    const IdleBlank& idle_blank() const noexcept { return idle_; }

    static constexpr std::int64_t kModeSettleMs = 250;

    const svc::adv7513::ProbeReport& i2c_probe() const noexcept { return seat_.probe(); }

    bool i2c_attached() const noexcept { return seat_.attached(); }

    static bool pll_usable(const svc::PllParams& p) noexcept { return p.c != 0u; }

    const svc::VideoService& video() const noexcept { return *video_; }

    svc::VideoService& video() noexcept { return *video_; }

    using I2cOutcome = VideoI2cOutcome;

    static const char* i2c_outcome_name(I2cOutcome o) noexcept {
        switch (o) {
            case I2cOutcome::Unwired:
                return "unwired";
            case I2cOutcome::Absent:
                return "absent";
            case I2cOutcome::Ambiguous:
                return "ambiguous";
            case I2cOutcome::Present:
                return "present";
            case I2cOutcome::Configured:
                return "configured";
            case I2cOutcome::Verified:
                return "verified";
            case I2cOutcome::Noemit:
                return "noemit";
            case I2cOutcome::Failed:
                return "failed";
        }
        return "unknown";
    }

    static constexpr std::uint8_t kModeNever = kVideoModeNever;
    static constexpr std::uint8_t kModeCustom = kVideoModeCustom;

    using Stats = VideoPumpStats;

    [[nodiscard]] Stats stats() const noexcept { return stats_cell_.sample().value; }

    [[nodiscard]] bool output_locked() const noexcept { return output_locked_; }

    [[nodiscard]] const VideoStatsCell& stats_cell() const noexcept { return stats_cell_; }

    std::optional<Error> last_error() const noexcept { return last_error_; }

    std::string_view resolved_spec() const noexcept { return std::string_view{spec_, spec_len_}; }

    std::int64_t last_apply_ns() const noexcept { return last_apply_ns_; }

private:
    struct FamilyMode {
        bool configured = false;
        std::uint8_t index = kVideoModeCustom;
        svc::video_timing::StoredMode stored{};
        bool have_raw_pll = false;
        svc::PllBlock raw_pll{};
    };

    void note_i2c(I2cOutcome o) noexcept;
    [[nodiscard]] Adv7513Seat::Host seat_host() noexcept;

    void take_edge(Adv7513Seat::Edge e) noexcept;
    [[nodiscard]] Ex<void> apply_now();

    void resolve_families();
    FamilyMode resolve_one(std::string_view spec, bool count_fallback);
    FamilyMode& family(svc::VmodeFamily f) noexcept;

    [[nodiscard]] Ex<void> apply_family(svc::VmodeFamily f, double fpix_mhz);

    void service_family_switch();
    void service_recovery();
    void service_mode_phase();

    void service_idle_blank();
    void load_spec_from_ini();

    void load_filters();
    void latch_spec(std::string_view spec) noexcept;

    const svc::Vfs& vfs_;
    const os::IClock& clock_;

    Adv7513Seat seat_;
    VideoWire wire_;
    std::optional<svc::VideoService> video_;

    VideoIni ini_{};
    VideoIni::Force ini_force_{};

    Stats stats_{};

    VideoStatsCell stats_cell_{};
    VideoGeometryCell geo_cell_{};
    void publish_monitoring() noexcept;

    struct PublishOnExit {
        VideoPump* self;

        ~PublishOnExit() { self->publish_monitoring(); }
    };

    std::optional<Error> last_error_{};

    bool i2c_held_ = false;
    bool pending_ = false;
    bool spec_valid_ = false;
    bool ini_read_ = false;
    bool override_latched_ = false;
    bool support_fhd_ = false;
    bool mode_pending_ = false;
    const IActivitySource& activity_;
    IdleBlank idle_{};
    std::uint32_t emit_watermark_ = 0;
    std::int64_t stage_ns_ = 0;
    svc::Modeline pending_mode_{};
    std::uint16_t spec_len_ = 0;
    std::int64_t last_apply_ns_ = -1;
    std::uint32_t geo_polls_ = 0;
    std::uint32_t geo_edges_ = 0;
    std::uint32_t geo_gen_seen_ = 0;
    std::uint16_t scrw_ = 0;
    std::uint16_t scrh_ = 0;
    std::uint8_t vscale_mode_ = 0;
    std::uint8_t vscale_border_ = 0;
    bool front_end_ = false;

    std::uint8_t filter_mode_ = 0;
    bool filters_pending_ = false;

    std::array<FamilyMode, 3> families_{};
    bool families_valid_ = false;

    bool output_locked_ = false;
    char spec_[kSpecMax]{};

    char core_name_[64]{};
    std::uint8_t core_name_len_ = 0;
};

}
