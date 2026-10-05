// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "app/activity_source.h"
#include "app/event.h"
#include "app/idle_blank.h"
#include "app/video_wire.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "os/clock.h"
#include "hal/hdmi_int.h"
#include "hal/video_out_decl.h"
#include "os/nanosleep_delay.h"
#include "svc/adv7513_breaker.h"
#include "svc/adv7513_bus.h"
#include "svc/adv7513_init_options.h"
#include "svc/adv7513_linux_adapter.h"
#include "svc/adv7513_probe_report.h"
#include "svc/adv7513_sub_map.h"
#include "svc/i2c_adapter.h"
#include "svc/pll_params.h"
#include "svc/video_service.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class HpsFramebuffer;

enum class VideoI2cOutcome : std::uint8_t {
    Unwired = 0,
    Absent,
    Ambiguous,
    Present,
    Configured,
    Verified,
    Noemit,
    Failed,
};

inline constexpr std::uint8_t kVideoModeNever = 0xFFu;
inline constexpr std::uint8_t kVideoModeCustom = 0xFEu;

struct VideoPumpStats {
    std::uint32_t edges = 0;
    std::uint32_t routed = 0;

    std::uint32_t empty_specs = 0;

    std::uint32_t fb_cmds_dropped = 0;
    std::uint32_t applies = 0;
    std::uint32_t abandoned = 0;
    std::uint32_t ini_reads = 0;
    std::uint32_t parse_fallbacks = 0;
    std::uint32_t solve_failures = 0;
    std::uint32_t zero_divider = 0;
    std::uint32_t approximated = 0;
    std::uint32_t stage_failures = 0;

    std::uint8_t last_mode_index = kVideoModeNever;
    std::uint32_t last_fpix_khz = 0;

    VideoI2cOutcome i2c = VideoI2cOutcome::Unwired;

    std::uint8_t i2c_bus = 0xFFu;
    std::uint32_t i2c_writes = 0;
    std::uint32_t i2c_errors = 0;
    std::uint8_t i2c_fail_reg = 0;

    std::uint8_t i2c_first_op = 0;
    std::uint8_t i2c_first_reg = 0;
    std::uint16_t i2c_first_errno = 0;
    std::uint16_t i2c_last_errno = 0;
    std::uint32_t i2c_verify_errors = 0;
    std::uint32_t i2c_poll_errors = 0;
    std::uint32_t i2c_reprobes = 0;
    std::uint32_t i2c_trips = 0;
    bool i2c_breaker_open = false;

    std::uint32_t spd_sends = 0;
    std::uint32_t spd_disables = 0;
    std::uint32_t spd_skips_dv = 0;
    std::uint32_t spd_noname = 0;
    std::uint8_t spd_fail_reg = 0;

    std::uint32_t filter_loads = 0;
    std::uint32_t family_switches = 0;
};

struct VideoGeometryRecord {
    bool valid = false;
    bool fb_en = false;

    bool rotated = false;
    std::uint16_t res = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t vtime = 0;
    std::uint32_t core_seq = 0;
};

using VideoStatsCell = xthread::Telemetry<VideoPumpStats, SeatTag::Ui>;
using VideoGeometryCell = xthread::Telemetry<VideoGeometryRecord, SeatTag::Ui>;

class VideoPump : public hal::IHdmiInterrupt {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::size_t kSpecMax = 1024;

    VideoPump(const svc::Vfs& vfs, const os::IClock& clock,
              const hal::VideoOutDecl& board) noexcept;

    VideoPump(const VideoPump&) = delete;
    VideoPump& operator=(const VideoPump&) = delete;

    [[nodiscard]] bool take_video_mode(std::string_view spec) noexcept;

    [[nodiscard]] bool take_fb_cmd(std::string_view line) noexcept;

    void set_hps_framebuffer(HpsFramebuffer* fb) noexcept { hps_fb_ = fb; }

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

    void resample_geometry() noexcept {
        if (video_.has_value()) video_->request_force_sample();
    }

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
    [[nodiscard]] std::uint8_t vsync_adjust() const noexcept { return vsync_adjust_; }

    [[nodiscard]] bool direct_video() const noexcept { return direct_video_; }
    [[nodiscard]] std::uint8_t direct_video_ini() const noexcept { return direct_video_ini_; }

    [[nodiscard]] bool ini_read() const noexcept { return ini_read_; }
    [[nodiscard]] std::uint16_t output_width() const noexcept { return scrw_; }
    [[nodiscard]] std::uint16_t output_height() const noexcept { return scrh_; }

    void set_core_name(std::string_view name) noexcept;

    void set_mode_override(std::string_view spec) noexcept;

    void set_i2c_adapter(svc::adv7513::II2cAdapter& a) noexcept;

    void set_hdmi_int_source(const hal::IHdmiInterrupt& s) noexcept { hdmi_int_ = &s; }

    bool hdmi_int_asserted() const override;

    void set_activity_source(const IActivitySource& s) noexcept { activity_ = &s; }
    const IdleBlank& idle_blank() const noexcept { return idle_; }

    static constexpr std::int64_t kModeSettleMs = 250;

    static constexpr std::uint32_t kReprobeFirstMs = 1'000;
    static constexpr std::uint32_t kReprobeMaxMs = 64'000;

    const svc::adv7513::ProbeReport& i2c_probe() const noexcept { return probe_; }
    bool i2c_attached() const noexcept { return adv_.has_value(); }

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
        svc::VideoService::StoredMode stored{};
        bool have_raw_pll = false;
        svc::PllBlock raw_pll{};
    };

    void note_i2c(I2cOutcome o) noexcept;
    Ex<void> apply_now();

    void resolve_families();
    FamilyMode resolve_one(std::string_view spec, bool count_fallback);
    const FamilyMode& family(svc::VmodeFamily f) const noexcept;

    [[nodiscard]] Ex<void> apply_family(svc::VmodeFamily f, double fpix_mhz);

    void service_family_switch();
    void configure_transmitter();

    bool attach_transmitter();
    void service_recovery();
    void schedule_reprobe() noexcept;
    void sync_bus_counters() noexcept;
    void service_mode_phase();

    void service_hotplug();

    void service_idle_blank();

    void bind_adv7513_io() noexcept;
    void rebind_sub_maps() noexcept;
    void send_spd_packet();
    void load_spec_from_ini();

    void load_filters();
    void latch_spec(std::string_view spec) noexcept;

    const svc::Vfs* vfs_;
    const os::IClock* clock_;
    hal::VideoOutDecl board_;

    std::optional<svc::VideoService> video_{};
    VideoWire wire_{};

    svc::adv7513::LinuxAdapter i2c_backend_{};
    svc::adv7513::II2cAdapter* adapter_ = nullptr;

    svc::adv7513::I2cBreaker breaker_{i2c_backend_};

    svc::adv7513::SubMap main_map_{};
    svc::adv7513::SubMap edid_map_{};
    svc::adv7513::SubMap cec_map_{};
    os::NanosleepDelay delay_{};
    std::optional<svc::adv7513::Adv7513Bus> adv_{};
    svc::adv7513::ProbeReport probe_{};
    svc::adv7513::InitOptions init_opts_{};

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
    bool probed_ = false;
    std::int64_t next_probe_ns_ = 0;
    std::uint32_t probe_backoff_ms_ = kReprobeFirstMs;
    std::int64_t attached_ns_ = 0;

    std::uint32_t writes_base_ = 0;
    std::uint32_t errors_base_ = 0;
    bool mode_pending_ = false;
    bool init_pending_ = false;
    bool init_ok_ = false;

    bool ints_armed_ = false;
    std::uint32_t prelude_done_seen_ = 0;
    std::uint32_t hpd_gen_seen_ = 0;
    std::uint32_t audio_gen_seen_ = 0;
    std::int64_t next_hpd_ns_ = 0;
    const hal::IHdmiInterrupt* hdmi_int_ = nullptr;
    const IActivitySource* activity_ = nullptr;
    IdleBlank idle_{};
    bool spd_direct_video_ = false;
    std::uint8_t spd_quirk_ = 0;
    std::uint32_t emit_watermark_ = 0;
    std::int64_t stage_ns_ = 0;
    svc::Modeline pending_mode_{};
    std::uint16_t spec_len_ = 0;
    std::int64_t last_apply_ns_ = -1;
    std::uint32_t geo_polls_ = 0;
    std::uint32_t geo_edges_ = 0;
    std::uint32_t geo_gen_seen_ = 0;
    HpsFramebuffer* hps_fb_ = nullptr;
    std::uint16_t scrw_ = 0;
    std::uint16_t scrh_ = 0;
    std::uint8_t vscale_mode_ = 0;
    std::uint8_t vscale_border_ = 0;
    bool front_end_ = false;

    std::array<std::string, 4> vfilter_seeds_{};
    std::uint8_t filter_mode_ = 0;
    bool filters_pending_ = false;

    std::string spec_pal_{};
    std::string spec_ntsc_{};
    FamilyMode fam_def_{};
    FamilyMode fam_pal_{};
    FamilyMode fam_ntsc_{};
    bool families_valid_ = false;

    bool output_locked_ = false;
    std::uint8_t vsync_adjust_ = 0;
    std::optional<std::uint8_t> vsync_force_{};
    double refresh_min_ = 0.0;
    double refresh_max_ = 0.0;
    bool direct_video_ = false;
    std::uint8_t direct_video_ini_ = 0;
    bool replay_ini_ = false;
    bool strict_direct_video_ = false;
    bool menu_pal_ = false;
    bool forced_scandoubler_ = false;
    std::uint16_t ar_cust_[4]{};
    char spec_[kSpecMax]{};

    char core_name_[64]{};
    std::uint8_t core_name_len_ = 0;
};

}  // namespace mister::app
