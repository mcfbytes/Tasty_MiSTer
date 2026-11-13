// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "app/staged_frame.h"
#include "infra/seat.h"
#include "os/raw_monotonic_clock.h"
#include "proto/types.h"
#include "svc/afilter_parser.h"
#include "svc/audio_service.h"
#include "svc/filter_wire_image.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class VideoWire;

class AudioPump {
    TASTY_SEAT_RESIDENT(Ui);

public:
    AudioPump(const svc::Vfs& vfs, VideoWire& wire) noexcept : vfs_(vfs), wire_(wire) {}

    void set_mute(bool on) noexcept;
    void set_attenuation(proto::Attenuation a) noexcept;
    void nudge(proto::VolumeStep step) noexcept;
    void toggle_mute() noexcept;

    void step_core_volume(svc::VolumeStep step) noexcept;

    [[nodiscard]] svc::AfilterEnable filter_enable() const noexcept {
        return model_.filter_enable();
    }

    [[nodiscard]] std::string_view filter_name() const noexcept { return filter_name_; }

    void set_filter_enabled(bool on) noexcept;
    void set_filter(std::string_view name) noexcept;

    void on_runtime_probe_reply(proto::ReplyWord r, proto::StageGen gen) noexcept;

    [[nodiscard]] bool owns_runtime(proto::StageGen gen) const noexcept;

    void load_for_core(std::string_view core, bool front_end, std::string_view afilter_default);

    [[nodiscard]] StagedFrame::AudioTail tail(bool front_end) const noexcept;

    void on_probe_reply(proto::ReplyWord r, bool front_end) noexcept;

    void tick(os::RawMonotonicClock::time_point now) noexcept;

    [[nodiscard]] svc::AudioService& model() noexcept { return model_; }

private:
    void note_change_() noexcept { save_owed_ = true; }

    void stage_volume_() noexcept;

    void stage_filter_push_() noexcept;

    void reload_filter_file_(bool enabled) noexcept;

    void save_filter_cfg_() const noexcept;

    const svc::Vfs& vfs_;
    VideoWire& wire_;
    svc::AfilterParser parser_{};
    svc::AudioService model_{svc::AudioService::create()};

    svc::FilterWireImage words_{};
    AfilterPushForm push_ = AfilterPushForm::Byte;

    std::string core_{};

    std::string filter_name_{};

    bool front_end_ = false;

    std::array<proto::StageGen, 4> runtime_gens_{};

    bool save_owed_ = false;
    std::optional<os::RawMonotonicClock::time_point> save_at_{};
};

}
