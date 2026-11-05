// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "proto/link_frame.h"
#include "svc/adv7513_io.h"
#include "svc/cec_state_machine.h"
#include "svc/edid_store.h"
#include "svc/geometry_gate.h"
#include "svc/hdmi_link.h"
#include "svc/modeline.h"
#include "svc/parsed_mode.h"
#include "svc/pll_solver.h"
#include "svc/types.h"
#include "svc/video_sample.h"
#include "svc/video_timing.h"
#include "infra/seat.h"
#include "proto/uio_opcodes.h"

namespace mister::svc {

class IFilterResend;

inline constexpr std::size_t kVideoWireWords = 26;

[[nodiscard]] Ex<void> emit_video_words(
    proto::ILinkFrame& frame,
    proto::UioOp opcode,
    std::span<const std::uint16_t> words
);

[[nodiscard]] Ex<std::uint16_t> emit_fbuf(proto::ILinkFrame& frame, std::uint16_t word);

[[nodiscard]] Ex<void> emit_ar_custom(
    proto::ILinkFrame& frame,
    std::span<const std::uint16_t> words
);

[[nodiscard]] Ex<void> emit_filter_coeff_chunk(
    proto::ILinkFrame& frame,
    std::span<const std::uint16_t> chunk
);

inline constexpr std::size_t kFilterPhases = 256;
inline constexpr std::size_t kFilterTaps = 4;
inline constexpr std::size_t kFilterBanks = 2;

inline constexpr std::size_t kFilterCoeffWords = 4096;

constexpr std::size_t filter_coeff_word_count(std::uint16_t ver) noexcept {

    constexpr std::uint16_t kEncodingMask = 0x3;
    constexpr std::uint16_t kFullPrecisionBit = 0x4;
    constexpr std::uint16_t kEncodingBytes = 1;

    constexpr std::size_t kBytePhaseStep = 16;
    constexpr std::size_t kCoarsePhaseStep = 4;

    constexpr std::size_t kWordsPerEntry = 2;
    switch (ver & kEncodingMask) {
        case kEncodingBytes:
            return kFilterBanks * (kFilterPhases / kBytePhaseStep) * kFilterTaps;
        case 2:
        case 3: {
            const std::size_t skip = (ver & kFullPrecisionBit) != 0 ? 1u : kCoarsePhaseStep;
            return kFilterBanks * (kFilterPhases / skip) * kFilterTaps * kWordsPerEntry;
        }
        default:
            return 0;
    }
}

constexpr std::uint16_t filter_coeff_word(std::uint16_t ver, std::size_t i) noexcept {
    constexpr std::uint16_t kEncodingMask = 0x3;
    constexpr std::uint16_t kFullPrecisionBit = 0x4;
    constexpr std::uint16_t kEncodingBytes = 1;
    constexpr std::size_t kBytePhaseStep = 16;
    constexpr std::size_t kCoarsePhaseStep = 4;
    constexpr std::size_t kCoarsePhases = 64;
    constexpr std::size_t kWordsPerEntry = 2;

    constexpr unsigned kAddrShift = 9;
    constexpr std::uint32_t kValueMask = 0x1FF;

    constexpr std::uint16_t kHotByte = 128;
    constexpr std::uint16_t kHotCoarse = 128;
    constexpr std::uint16_t kHotFull = 256;

    constexpr std::size_t kLowHalfHotTap = 1;
    constexpr std::size_t kHighHalfHotTap = 2;
    const std::size_t enc = ver & kEncodingMask;
    if (enc == kEncodingBytes) {
        const std::size_t per_bank = (kFilterPhases / kBytePhaseStep) * kFilterTaps;
        const std::size_t bank = i / per_bank;
        const std::size_t within = i % per_bank;
        const std::size_t iter = within / kFilterTaps;
        const std::size_t tap = within % kFilterTaps;
        const std::size_t addr = bank * per_bank + iter * kFilterTaps + tap;
        const std::size_t phase = iter * kBytePhaseStep;

        const std::uint16_t val = (tap
                                   == (phase < kFilterPhases / 2 ? kLowHalfHotTap
                                                                 : kHighHalfHotTap))
            ? kHotByte
            : std::uint16_t{0};
        return static_cast<std::uint16_t>((val & kValueMask) | (addr << kAddrShift));
    }
    const bool full = (ver & kFullPrecisionBit) != 0;
    const std::size_t skip = full ? 1u : kCoarsePhaseStep;
    const std::size_t stride = full ? kFilterPhases * kFilterTaps : kCoarsePhases * kFilterTaps;
    const std::size_t per_bank = (kFilterPhases / skip) * kFilterTaps;
    const std::size_t pair = i / kWordsPerEntry;
    const std::size_t bank = pair / per_bank;
    const std::size_t within = pair % per_bank;
    const std::size_t iter = within / kFilterTaps;
    const std::size_t tap = within % kFilterTaps;
    const bool is_address_word = i % kWordsPerEntry == 0;
    if (is_address_word) {
        return static_cast<std::uint16_t>(bank * stride + iter * kFilterTaps + tap);
    }
    const std::size_t phase = iter * skip;
    const std::size_t hot = phase < kFilterPhases / 2 ? kLowHalfHotTap : kHighHalfHotTap;
    if (tap != hot)
        return 0;

    return full ? kHotFull : kHotCoarse;
}

Modeline calculate_cvt(
    std::uint32_t h_pixels,
    std::uint32_t v_lines,
    float refresh_rate,
    std::uint8_t reduced_blanking,
    bool supports_pr
);

inline constexpr std::size_t kYcMaxEntries = 20;

struct YcMode {
    std::array<char, 64> key{};
    std::int64_t phase_inc = 0;
    std::string_view key_view() const noexcept;
};

std::vector<YcMode> parse_yc(std::string_view text, std::string_view core_name);

std::optional<std::int64_t> yc_lookup(
    std::span<const YcMode> table,
    std::string_view key,
    std::string_view key_expanded
);

class VideoService {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Wiring;

    VideoService(const VideoService&) = delete;
    VideoService& operator=(const VideoService&) = delete;

    explicit VideoService(Wiring w, adv7513::Io io = adv7513::kUnboundIo) noexcept;
    [[nodiscard]] static VideoService create(Wiring w, adv7513::Io io = adv7513::kUnboundIo);
    [[nodiscard]] static VideoService create();

    [[nodiscard]] Ex<video_timing::ParsedMode> parse_mode(std::string_view spec) const;

    [[nodiscard]] Ex<void> apply(const Modeline& m, PllBlock pll, Modeline::WireOptions opt);

    [[nodiscard]] Ex<void> poll_resolution();

    [[nodiscard]] Ex<void> poll_hotplug() { return hdmi_.poll_hotplug(); }

    EdidStore& edid() noexcept { return hdmi_.edid(); }

    CecStateMachine& cec() noexcept { return hdmi_.cec(); }

    class IVideoWireSink {
    public:
        virtual ~IVideoWireSink() = default;
        [[nodiscard]] virtual Ex<void> publish(
            proto::UioOp opcode,
            std::span<const std::uint16_t> words
        ) = 0;

    protected:
        IVideoWireSink() = default;
        IVideoWireSink(const IVideoWireSink&) = default;
        IVideoWireSink& operator=(const IVideoWireSink&) = default;
    };

    class IResolutionSampler {
    public:
        virtual ~IResolutionSampler() = default;
        [[nodiscard]] virtual Ex<VideoSample> sample(bool force) = 0;

    protected:
        IResolutionSampler() = default;
        IResolutionSampler(const IResolutionSampler&) = default;
        IResolutionSampler& operator=(const IResolutionSampler&) = default;
    };

    struct Wiring {
        infra::OptRef<IVideoWireSink> sink{};
        infra::OptRef<IResolutionSampler> sampler{};
    };

    void attach() noexcept { hdmi_.attach(); }

    void set_hdmi_capable(bool v) noexcept { hdmi_.set_hdmi_capable(v); }

    bool hdmi_capable() const noexcept { return hdmi_.hdmi_capable(); }

    void set_supports_pr(bool v) noexcept { supports_pr_ = v; }

    bool supports_pr() const noexcept { return supports_pr_; }

    const VideoSample& info() const noexcept { return gate_.last; }

    Generation resolution_generation() const noexcept { return res_gen_; }

    [[nodiscard]] Ex<void> request_power(bool on) { return hdmi_.request_power(on); }

    bool power_requested() const noexcept { return hdmi_.power_requested(); }

    bool init_owed() const noexcept { return hdmi_.init_owed(); }

    void clear_init_owed() noexcept { hdmi_.clear_init_owed(); }

    Generation audio_generation() const noexcept { return hdmi_.audio_generation(); }

    Generation hotplug_generation() const noexcept { return hdmi_.hotplug_generation(); }

    bool sink_powered() const noexcept { return hdmi_.sink_powered(); }

    VFilter vfilter() const noexcept { return vfilter_; }

    void set_filter_slots(std::span<const bool> enabled) noexcept;

    void request_force_sample() noexcept { force_next_ = true; }

    void reset_resolution_gate() noexcept {
        gate_ = GeometryGate{};
        force_next_ = true;
    }

private:
    HdmiLink hdmi_;

    infra::OptRef<IVideoWireSink> sink_{};
    infra::OptRef<IResolutionSampler> sampler_{};

    GeometryGate gate_{};
    Generation res_gen_{};
    bool supports_pr_ = true;
    bool force_next_ = true;
    VFilter vfilter_ = VFilter::Horz;
    std::array<bool, 4> filter_slots_{};
};

}
