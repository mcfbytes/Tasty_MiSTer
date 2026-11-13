// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/link_frame.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"
#include "proto/user_io/declared_select.h"
#include "svc/types.h"

namespace mister::svc {

struct VolumeByte {
    std::uint8_t v = 0;
    static constexpr std::uint8_t kAttenMask = 0x07;
    static constexpr std::uint8_t kMuteBit = 0x10;
    static constexpr std::uint8_t kValidMask = 0x17;
    static constexpr std::uint8_t kMaxAttenuation = proto::Attenuation::kMax;

    [[nodiscard]] constexpr bool muted() const noexcept { return (v & kMuteBit) != 0; }

    [[nodiscard]] constexpr proto::Attenuation attenuation() const noexcept {
        return proto::Attenuation{static_cast<std::uint8_t>(v & kAttenMask)};
    }

    [[nodiscard]] constexpr bool quietest() const noexcept {
        return (v & kAttenMask) == kMaxAttenuation;
    }

    [[nodiscard]] constexpr bool loudest() const noexcept { return (v & kAttenMask) == 0; }

    [[nodiscard]] constexpr std::uint8_t kept_bits() const noexcept { return v & kValidMask; }

    [[nodiscard]] constexpr VolumeByte valid() const noexcept {
        return VolumeByte{static_cast<std::uint8_t>(v & kValidMask)};
    }

    [[nodiscard]] constexpr VolumeByte with_mute(bool on) const noexcept {
        return VolumeByte{static_cast<std::uint8_t>((v & kAttenMask) | (on ? kMuteBit : 0))};
    }

    [[nodiscard]] constexpr VolumeByte with_attenuation(proto::Attenuation a) const noexcept {
        return VolumeByte{static_cast<std::uint8_t>((v & kMuteBit) | (a.v & kAttenMask))};
    }

    [[nodiscard]] constexpr VolumeByte toggled_mute() const noexcept {
        return VolumeByte{static_cast<std::uint8_t>(v ^ kMuteBit)}.valid();
    }

    [[nodiscard]] constexpr VolumeByte without_mute() const noexcept {
        return VolumeByte{static_cast<std::uint8_t>(v & ~kMuteBit)};
    }

    [[nodiscard]] constexpr VolumeByte capped_under(CoreVolume core) const noexcept;

    [[nodiscard]] constexpr std::uint8_t summed_with(CoreVolume core) const noexcept;

    [[nodiscard]] constexpr VolumeByte quieter() const noexcept {
        const std::uint8_t att = static_cast<std::uint8_t>(v & kAttenMask);
        if (att >= kMaxAttenuation)
            return *this;
        return VolumeByte{static_cast<std::uint8_t>(v + 1)};
    }

    [[nodiscard]] constexpr VolumeByte louder() const noexcept {
        if ((v & kAttenMask) == 0)
            return *this;
        return VolumeByte{static_cast<std::uint8_t>(v - 1)};
    }

    friend constexpr bool operator==(VolumeByte, VolumeByte) = default;
};

static_assert(sizeof(VolumeByte) == 1, "the wire attenuation is one byte");

static_assert(VolumeByte::kAttenMask == CoreVolume::kAttenMask);

constexpr VolumeByte VolumeByte::capped_under(CoreVolume core) const noexcept {
    if (muted() || static_cast<unsigned>(v) + core.v <= kMaxAttenuation)
        return *this;
    return VolumeByte{static_cast<std::uint8_t>(kMaxAttenuation - core.v)};
}

constexpr std::uint8_t VolumeByte::summed_with(CoreVolume core) const noexcept {
    return static_cast<std::uint8_t>(v + core.field_bits());
}

static_assert(sizeof(CoreVolume) == 1, "the core volume is one byte");

class AudioService {
    TASTY_SEAT_RESIDENT(Ui);

public:
    [[nodiscard]] static AudioService create() noexcept;

    void set_mute(bool on) noexcept;
    void set_attenuation(proto::Attenuation attenuation) noexcept;
    void nudge(proto::VolumeStep step) noexcept;
    void toggle_mute() noexcept;

    void load_volume(VolumeByte raw) noexcept { vol_ = raw.valid(); }

    void load_core_volume(CoreVolume raw) noexcept;

    std::uint8_t volume_byte() const noexcept { return vol_.kept_bits(); }

    bool muted() const noexcept { return vol_.muted(); }

    proto::Attenuation attenuation() const noexcept { return vol_.attenuation(); }

    void step_core_volume(VolumeStep step);

    std::uint8_t core_volume_byte() const noexcept { return corevol_.field_bits(); }

    bool has_filter() const noexcept { return has_filter_; }

    bool has_boost() const noexcept { return has_boost_; }

    [[nodiscard]] AfilterEnable filter_enable() const noexcept;

    void load_filter_enable(bool on) noexcept { filter_enabled_ = on; }

    void set_filter_enabled(bool on) noexcept { filter_enabled_ = on; }

    void accept_probe_reply(proto::ReplyWord r, bool front_end) noexcept;

    [[nodiscard]] CoreVolume lead_with_boost() const noexcept;
    [[nodiscard]] CoreVolume lead_without_boost() const noexcept;
    [[nodiscard]] AudvolByte audvol_with_filter() const noexcept;
    [[nodiscard]] AudvolByte audvol_without_filter() const noexcept;

    [[nodiscard]] AudvolByte settle_after_probe() noexcept;

    [[nodiscard]] AudvolByte send_volume() noexcept;

private:
    AudioService() = default;
    VolumeByte vol_{};
    CoreVolume corevol_{};
    bool has_filter_ = false;
    bool has_boost_ = false;
    bool filter_enabled_ = false;
};

[[nodiscard]] Ex<void> emit_afilter_stream(
    proto::ILinkFrame& frame,
    CoreVolume lead,
    std::span<const std::uint16_t> words
);

[[nodiscard]] Ex<void> emit_afilter_volume(proto::ILinkFrame& frame, CoreVolume vol);

[[nodiscard]] Ex<void> emit_audvol(proto::ILinkFrame& frame, AudvolByte vol);

}
