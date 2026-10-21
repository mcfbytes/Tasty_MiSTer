// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "app/video_pump_stats.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "hal/hdmi_int.h"
#include "hal/video_out_decl.h"
#include "os/clock.h"
#include "os/nanosleep_delay.h"
#include "svc/adv7513_breaker.h"
#include "svc/adv7513_bus.h"
#include "svc/adv7513_init_options.h"
#include "svc/adv7513_io.h"
#include "svc/adv7513_linux_adapter.h"
#include "svc/adv7513_probe_report.h"
#include "svc/adv7513_sub_map.h"
#include "svc/i2c_adapter.h"
#include "svc/modeline.h"

namespace mister::svc {
class VideoService;
}

namespace mister::app {

class VideoWire;

class Adv7513Seat final : public hal::IHdmiInterrupt {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Host {
        svc::VideoService& video;
        const VideoWire& wire;
        VideoPumpStats& stats;
        std::optional<Error>& last_error;
    };

    enum class Edge : std::uint8_t { None, Lost, Owed };

    struct Spd {
        std::string_view core_name;
        bool direct_video = false;
        std::uint8_t quirk = 0;
    };

    static constexpr std::uint32_t kReprobeFirstMs = 1'000;
    static constexpr std::uint32_t kReprobeMaxMs = 64'000;

    Adv7513Seat(
        const os::IClock& clock,
        const hal::VideoOutDecl& board,
        infra::OptRef<svc::adv7513::II2cAdapter> i2c,
        const hal::IHdmiInterrupt& hdmi_int
    ) noexcept;

    Adv7513Seat(const Adv7513Seat&) = delete;
    Adv7513Seat& operator=(const Adv7513Seat&) = delete;

    [[nodiscard]] svc::adv7513::Io io() noexcept;

    bool hdmi_int_asserted() const override;

    void on_core_loaded(Host h);

    [[nodiscard]] Edge configure(Host h, const svc::adv7513::InitOptions& init, const Spd& spd);

    [[nodiscard]] Edge recover(Host h);

    [[nodiscard]] Edge hotplug(Host h, const svc::adv7513::InitOptions& init);

    [[nodiscard]] std::optional<VideoI2cOutcome> program_mode(Host h, const svc::Modeline& m);

    void report(VideoPumpStats& stats) noexcept;

    const svc::adv7513::ProbeReport& probe() const noexcept { return probe_; }

    bool attached() const noexcept { return adv_.has_value(); }

    bool init_ok() const noexcept { return init_ok_; }

private:
    bool attach(Host h);
    void schedule_reprobe() noexcept;
    void sync_counters(VideoPumpStats& stats) noexcept;
    void send_spd(Host h, const Spd& spd);

    const os::IClock& clock_;
    hal::VideoOutDecl board_;
    const hal::IHdmiInterrupt& hdmi_int_;

    svc::adv7513::LinuxAdapter i2c_backend_{};
    svc::adv7513::II2cAdapter& adapter_;

    svc::adv7513::I2cBreaker breaker_;

    svc::adv7513::SubMap main_map_;
    svc::adv7513::SubMap edid_map_;
    svc::adv7513::SubMap cec_map_;
    os::NanosleepDelay delay_{};

    std::optional<svc::adv7513::Adv7513Bus> adv_{};
    svc::adv7513::ProbeReport probe_{};

    bool probed_ = false;
    std::int64_t next_probe_ns_ = 0;
    std::uint32_t probe_backoff_ms_ = kReprobeFirstMs;
    std::int64_t attached_ns_ = 0;

    std::uint32_t writes_base_ = 0;
    std::uint32_t errors_base_ = 0;
    bool init_pending_ = false;
    bool init_ok_ = false;

    bool ints_armed_ = false;
    std::uint32_t prelude_done_seen_ = 0;
    std::uint32_t hpd_gen_seen_ = 0;
    std::uint32_t audio_gen_seen_ = 0;
    std::int64_t next_hpd_ns_ = 0;
};

}
