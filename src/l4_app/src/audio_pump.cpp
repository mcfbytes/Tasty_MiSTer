// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/audio_pump.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "app/durable_write.h"
#include "app/stream_io.h"
#include "app/video_wire.h"
#include "infra/byte_view.h"
#include "infra/message_sum.h"
#include "svc/read_bounded.h"

namespace mister::app {

namespace {

constexpr std::string_view kConfigDir = "config/";

constexpr std::string_view kVolumeRel = "config/Volume.dat";

constexpr proto::FileSize kFilterCfgReadCap{1023};

[[nodiscard]] std::vector<std::byte> read_config(
    const svc::Vfs& vfs,
    std::string_view rel,
    proto::FileSize cap
) {
    auto open = stream_open(vfs, rel);
    if (!open)
        return {};
    auto bytes = svc::read_bounded(*open->f, cap.file_word<std::uint64_t>());
    if (!bytes)
        return {};
    return *bytes;
}

[[nodiscard]] std::string config_rel(std::string_view core, std::string_view suffix) {
    std::string rel;
    rel.reserve(kConfigDir.size() + core.size() + suffix.size());
    rel.append(kConfigDir);
    rel.append(core);
    rel.append(suffix);
    return rel;
}

}

void AudioPump::set_mute(bool on) noexcept {
    model_.set_mute(on);
    note_change_();
    stage_volume_();
}

void AudioPump::set_attenuation(proto::Attenuation a) noexcept {
    model_.set_attenuation(a);
    note_change_();
    stage_volume_();
}

void AudioPump::nudge(proto::VolumeStep step) noexcept {
    model_.nudge(step);
    note_change_();
    stage_volume_();
}

void AudioPump::toggle_mute() noexcept {
    model_.toggle_mute();
    note_change_();
    stage_volume_();
}

void AudioPump::stage_volume_() noexcept {

    const StagedFrame::AudVol frame{model_.send_volume()};
    const auto bound = wire_.bound_core();
    if (!bound)
        return;
    (void)wire_.stage(infra::make<StagedFrame>(frame), bound->gen);
}

void AudioPump::load_for_core(
    std::string_view core,
    bool front_end,
    std::string_view afilter_default
) {
    model_ = svc::AudioService::create();
    words_ = svc::FilterWireImage{};
    push_ = AfilterPushForm::Byte;
    core_.assign(core);
    filter_name_.clear();
    front_end_ = front_end;
    runtime_gens_ = {};
    save_owed_ = false;
    save_at_.reset();

    bool enabled = false;
    std::string name;
    const std::vector<std::byte> cfg = read_config(
        vfs_,
        config_rel(core, "_afilter.cfg"),
        kFilterCfgReadCap
    );
    if (!cfg.empty() && cfg[0] <= std::byte{1}) {
        enabled = cfg[0] != std::byte{0};

        std::size_t end = 1;
        while (end < cfg.size() && cfg[end] != std::byte{0})
            ++end;
        name.assign(infra::as_text(std::span<const std::byte>(cfg).subspan(1, end - 1)));
    } else if (!afilter_default.empty()) {
        enabled = true;
        name.assign(afilter_default);
    }

    const std::vector<std::byte> volume = read_config(vfs_, kVolumeRel, proto::FileSize{1});
    if (!volume.empty())
        model_.load_volume(svc::VolumeByte{std::to_integer<std::uint8_t>(volume[0])});
    if (!front_end) {
        const std::vector<std::byte> corevol = read_config(
            vfs_,
            config_rel(core, "_volume.cfg"),
            proto::FileSize{1}
        );
        if (!corevol.empty())
            model_.load_core_volume(svc::CoreVolume{std::to_integer<std::uint8_t>(corevol[0])});
    }

    model_.load_filter_enable(enabled);
    filter_name_.assign(name);
    reload_filter_file_(enabled);
}

void AudioPump::reload_filter_file_(bool enabled) noexcept {
    words_ = svc::FilterWireImage{};
    push_ = AfilterPushForm::Byte;
    if (!enabled || filter_name_.empty())
        return;
    const auto read = parser_.parse(vfs_, filter_name_);
    if (read && read->opened) {
        if (read->parse) {
            push_ = AfilterPushForm::Stream;
            words_ = *read->parse;
        } else {
            push_ = AfilterPushForm::Withheld;
        }
    }
}

void AudioPump::set_filter_enabled(bool on) noexcept {
    model_.set_filter_enabled(on);
    reload_filter_file_(on);
    save_filter_cfg_();
    stage_filter_push_();
}

void AudioPump::set_filter(std::string_view name) noexcept {
    filter_name_.assign(name);
    reload_filter_file_(model_.filter_enable() == svc::AfilterEnable::On);
    save_filter_cfg_();
    stage_filter_push_();
}

void AudioPump::step_core_volume(svc::VolumeStep step) noexcept {
    model_.step_core_volume(step);
    note_change_();
    if (model_.has_boost() || model_.has_filter()) {
        stage_filter_push_();
    } else {
        stage_volume_();
    }
}

void AudioPump::save_filter_cfg_() const noexcept {
    if (core_.empty())
        return;

    constexpr std::size_t kFilterCfgBytes = 1024;
    std::array<std::byte, kFilterCfgBytes> cfg{};
    cfg[0] = model_.filter_enable() == svc::AfilterEnable::On ? std::byte{1} : std::byte{0};

    const std::string_view name = filter_name_;
    const std::size_t room = cfg.size() - 1 - 1;
    const std::span<const std::byte> tail = infra::text_bytes(name.substr(0, room));
    std::copy(tail.begin(), tail.end(), cfg.begin() + 1);
    (void)durable_write(vfs_, config_rel(core_, "_afilter.cfg"), cfg);
}

void AudioPump::stage_filter_push_() noexcept {
    StagedFrame::AfilterPush frame{};
    frame.push = push_;
    frame.lead.if_set = model_.lead_with_boost();
    frame.lead.if_clear = model_.lead_without_boost();
    const std::span<const std::uint16_t> words = words_.words();
    frame.count = words_.count;
    for (std::size_t i = 0; i < words.size(); ++i)
        frame.words[i] = words[i];
    const auto bound = wire_.bound_core();
    if (!bound)
        return;
    if (!wire_.stage(infra::make<StagedFrame>(frame), bound->gen))
        return;
    const proto::StageGen gen = wire_.staged_generation();
    std::size_t free_slot = runtime_gens_.size();
    for (std::size_t i = 0; i < runtime_gens_.size(); ++i) {
        if (!runtime_gens_[i].is_set()) {
            free_slot = i;
            break;
        }
    }

    runtime_gens_[free_slot < runtime_gens_.size() ? free_slot : 0] = gen;
}

void AudioPump::on_runtime_probe_reply(proto::ReplyWord r, proto::StageGen gen) noexcept {
    for (std::size_t i = 0; i < runtime_gens_.size(); ++i)
        if (runtime_gens_[i].is_set() && runtime_gens_[i] == gen) {
            runtime_gens_[i] = {};
            break;
        }
    model_.accept_probe_reply(r, front_end_);
}

bool AudioPump::owns_runtime(proto::StageGen gen) const noexcept {
    for (const proto::StageGen mine : runtime_gens_)
        if (mine.is_set() && mine == gen)
            return true;
    return false;
}

StagedFrame::AudioTail AudioPump::tail(bool front_end) const noexcept {
    (void)front_end;
    StagedFrame::AudioTail t{};
    t.push = push_;
    t.lead.if_set = model_.lead_with_boost();
    t.lead.if_clear = model_.lead_without_boost();
    t.audvol.if_set = model_.audvol_with_filter();
    t.audvol.if_clear = model_.audvol_without_filter();
    const std::span<const std::uint16_t> words = words_.words();
    t.count = words_.count;
    for (std::size_t i = 0; i < words.size(); ++i)
        t.words[i] = words[i];
    return t;
}

void AudioPump::on_probe_reply(proto::ReplyWord r, bool front_end) noexcept {
    model_.accept_probe_reply(r, front_end);
    (void)model_.settle_after_probe();
}

void AudioPump::tick(os::RawMonotonicClock::time_point now) noexcept {
    if (!save_owed_)
        return;
    if (!save_at_) {
        save_at_ = now + std::chrono::seconds{1};
        return;
    }
    if (now < *save_at_)
        return;
    save_owed_ = false;
    save_at_.reset();

    const std::array<std::byte, 1> vol{std::byte{model_.volume_byte()}};
    (void)durable_write(vfs_, kVolumeRel, vol);
    if (core_.empty())
        return;
    const std::array<std::byte, 1> corevol{std::byte{model_.core_volume_byte()}};
    (void)durable_write(vfs_, config_rel(core_, "_volume.cfg"), corevol);
}

}
