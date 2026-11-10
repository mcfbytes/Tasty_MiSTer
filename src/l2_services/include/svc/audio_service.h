// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "proto/link_frame.h"
#include "proto/uio_opcodes.h"
#include "proto/types.h"

namespace mister::svc {

struct CoreVolume;

enum class VolumeStep : std::uint8_t { Quieter, Hold, Louder };

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

struct CoreVolume {
    std::uint8_t v = 0;
    static constexpr std::uint8_t kAttenMask = VolumeByte::kAttenMask;
    static constexpr std::uint8_t kBoostMask = 0x60;
    static constexpr std::uint8_t kBoostStep = 0x20;
    static constexpr std::uint8_t kBoostMax = 0x40;
    static constexpr std::uint8_t kAttenMax = 6;

    [[nodiscard]] constexpr std::uint8_t field_bits() const noexcept {
        return v & (kAttenMask | kBoostMask);
    }

    [[nodiscard]] constexpr CoreVolume attenuation_only() const noexcept {
        return CoreVolume{static_cast<std::uint8_t>(v & kAttenMask)};
    }

    [[nodiscard]] constexpr CoreVolume stepped(
        VolumeStep step,
        bool boost_available
    ) const noexcept {
        std::uint8_t out = v & kAttenMask;
        if (boost_available) {
            unsigned boost = v & kBoostMask;
            if (step == VolumeStep::Quieter) {
                if (boost != 0) {
                    out = 0;
                    boost -= kBoostStep;
                } else if (out < kAttenMax) {
                    ++out;
                }
            }
            if (step == VolumeStep::Louder) {
                if (boost != 0 || out == 0) {
                    boost += kBoostStep;
                    if (boost > kBoostMax)
                        boost = kBoostMax;
                } else if (out > 0) {
                    --out;
                }
            }
            out = static_cast<std::uint8_t>(out | boost);
        } else {
            if (step == VolumeStep::Quieter && out < kAttenMax)
                ++out;
            if (step == VolumeStep::Louder && out > 0)
                --out;
        }
        return CoreVolume{out};
    }

    friend constexpr bool operator==(CoreVolume, CoreVolume) = default;
};

constexpr VolumeByte VolumeByte::capped_under(CoreVolume core) const noexcept {
    if (muted() || static_cast<unsigned>(v) + core.v <= kMaxAttenuation)
        return *this;
    return VolumeByte{static_cast<std::uint8_t>(kMaxAttenuation - core.v)};
}

constexpr std::uint8_t VolumeByte::summed_with(CoreVolume core) const noexcept {
    return static_cast<std::uint8_t>(v + core.v);
}

static_assert(sizeof(CoreVolume) == 1, "the core volume is one byte");

inline constexpr std::size_t kFilterWireWords = 15;

inline constexpr int kFilterLines = 9;

inline constexpr std::int64_t kCoefScaleQ39 = 0x8000000000LL;
inline constexpr std::int32_t kCoefScaleQ21 = 0x200000;

struct FilterWordIndex {
    std::uint8_t v = 0;
    friend constexpr bool operator==(FilterWordIndex, FilterWordIndex) = default;
};

struct FilterWordCount {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr std::span<const std::uint16_t> prefix_of(
        std::span<const std::uint16_t> all
    ) const noexcept {
        return all.first(v);
    }

    [[nodiscard]] constexpr FilterWordIndex next_index() const noexcept {
        return FilterWordIndex{v};
    }

    [[nodiscard]] constexpr FilterWordCount with_one_more() const noexcept {
        FilterWordCount out = *this;
        ++out.v;
        return out;
    }

    friend constexpr bool operator==(FilterWordCount, FilterWordCount) = default;
};

struct FilterWireImage {

    [[nodiscard]] std::span<const std::uint16_t> words() const noexcept {
        return count.prefix_of(std::span<const std::uint16_t>(slots));
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return count.empty(); }

    FilterWordCount count{};
    std::array<std::uint16_t, kFilterWireWords> slots{};
};

inline void append_filter_word(FilterWireImage& image, std::uint16_t word) noexcept {
    infra::at_index(image.slots, image.count.next_index()) = word;
    image.count = image.count.with_one_more();
}

using AfilterParse = std::optional<FilterWireImage>;

class AudioService {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] static AudioService create() noexcept;

    void set_mute(bool on) noexcept;
    void set_attenuation(proto::Attenuation attenuation) noexcept;
    void nudge(proto::VolumeStep step) noexcept;
    void toggle_mute() noexcept;

    std::uint8_t volume_byte() const noexcept { return vol_.kept_bits(); }

    bool muted() const noexcept { return vol_.muted(); }

    proto::Attenuation attenuation() const noexcept { return vol_.attenuation(); }

    void set_core_volume(int cmd);

    std::uint8_t core_volume_byte() const noexcept { return corevol_.field_bits(); }

    [[nodiscard]] Ex<bool> probe_filter();

    bool has_filter() const noexcept { return has_filter_; }

    void accept_probe_reply(std::uint8_t raw_reply, bool front_end) noexcept;

    void frame_filter_image(const AfilterParse& parse);

    std::span<const std::uint16_t> filter_wire_words() const noexcept { return image_.words(); }

    [[nodiscard]] Ex<void> flush();

    std::uint8_t audvol_wire_byte() const noexcept { return pending_audvol_; }

private:
    AudioService() = default;
    VolumeByte vol_{};
    CoreVolume corevol_{};
    bool has_filter_ = false;
    bool has_boost_ = false;

    FilterWireImage image_{};
    std::uint8_t pending_audvol_ = 0;
};

struct AfilterParser {
    TASTY_SEAT_RESIDENT(Io);

    [[nodiscard]] static AfilterParser create() noexcept;

    [[nodiscard]] Ex<AfilterParse> parse(std::string_view name) const;
};

[[nodiscard]] Ex<void> emit_afilter_volume(proto::ILinkFrame& frame, std::uint8_t vol);

[[nodiscard]] Ex<void> emit_audvol(proto::ILinkFrame& frame, std::uint8_t vol);

}
