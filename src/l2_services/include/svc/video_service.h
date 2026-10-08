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
#include "hal/spi_transport.h"
#include "svc/adv7513_io.h"
#include "svc/cec_state_machine.h"
#include "svc/edid_store.h"
#include "svc/modeline.h"

#include "svc/pll_solver.h"
#include "svc/types.h"
#include "svc/video_sample.h"
#include "infra/seat.h"

namespace mister::svc {

struct GeometryGate;
class IFilterResend;

inline constexpr std::uint8_t kUioSetVideo = 0x20;
inline constexpr std::size_t kVideoWireWords = 26;

Ex<void> emit_video_words(hal::ISpiTransport& link, std::uint8_t opcode,
                          std::span<const std::uint16_t> words);

inline constexpr std::uint8_t kUioSetFbuf = 0x2F;
inline constexpr std::uint8_t kUioSetArCust = 0x3A;
inline constexpr std::uint8_t kUioShadowMask = 0x3E;
inline constexpr std::uint8_t kUioHdmiInt = 0x44;
inline constexpr std::uint8_t kUioSetGamma = 0x32;

Ex<std::uint16_t> emit_fbuf(hal::ISpiTransport& link, std::uint16_t word);

Ex<void> emit_ar_custom(hal::ISpiTransport& link, std::span<const std::uint16_t> words);

Ex<std::uint16_t> emit_filter_mode(hal::ISpiTransport& link, std::uint8_t mode);

Ex<void> emit_filter_coeff_chunk(hal::ISpiTransport& link, std::span<const std::uint16_t> chunk);

Ex<std::uint16_t> emit_shadow_mask(hal::ISpiTransport& link, std::uint16_t flag_word);

Ex<std::uint16_t> emit_hdmi_int_probe(hal::ISpiTransport& link);

Ex<std::uint16_t> emit_gamma_probe(hal::ISpiTransport& link);

inline constexpr std::size_t kFilterPhases = 256;
inline constexpr std::size_t kFilterTaps = 4;
inline constexpr std::size_t kFilterBanks = 2;

inline constexpr std::size_t kFilterCoeffWords = 4096;

constexpr std::size_t filter_coeff_word_count(std::uint16_t ver) noexcept {
    switch (ver & 0x3u) {
        case 1:
            return kFilterBanks * (kFilterPhases / 16) * kFilterTaps;
        case 2:
        case 3: {
            const std::size_t skip = (ver & 0x4u) != 0 ? 1u : 4u;
            return kFilterBanks * (kFilterPhases / skip) * kFilterTaps * 2u;
        }
        default:
            return 0;
    }
}

constexpr std::uint16_t filter_coeff_word(std::uint16_t ver, std::size_t i) noexcept {
    const std::size_t enc = ver & 0x3u;
    if (enc == 1u) {
        const std::size_t per_bank = (kFilterPhases / 16) * kFilterTaps;
        const std::size_t bank = i / per_bank;
        const std::size_t within = i % per_bank;
        const std::size_t iter = within / kFilterTaps;
        const std::size_t tap = within % kFilterTaps;
        const std::size_t addr = bank * 64u + iter * kFilterTaps + tap;
        const std::size_t phase = iter * 16u;
        const std::size_t hot = (phase < kFilterPhases / 2) ? 1u : 2u;

        const std::uint16_t val = (tap == hot) ? std::uint16_t{128} : std::uint16_t{0};
        return static_cast<std::uint16_t>((val & 0x1FFu) | (addr << 9));
    }
    const bool full = (ver & 0x4u) != 0;
    const std::size_t skip = full ? 1u : 4u;
    const std::size_t stride = full ? kFilterPhases * kFilterTaps : 64u * kFilterTaps;
    const std::size_t per_bank = (kFilterPhases / skip) * kFilterTaps;
    const std::size_t pair = i / 2;
    const std::size_t bank = pair / per_bank;
    const std::size_t within = pair % per_bank;
    const std::size_t iter = within / kFilterTaps;
    const std::size_t tap = within % kFilterTaps;
    if ((i & 1u) == 0u) {
        return static_cast<std::uint16_t>(bank * stride + iter * kFilterTaps + tap);
    }
    const std::size_t phase = iter * skip;
    const std::size_t hot = (phase < kFilterPhases / 2) ? 1u : 2u;
    if (tap != hot) return 0;

    return full ? std::uint16_t{256} : std::uint16_t{128};
}

enum class ModeRequest : std::uint8_t {
    PresetIndex,
    Triple,
    Nine,
    Eleven,
    RawModeline,
};

struct PresetMode {
    std::uint32_t vpar[8];
    double fpix_mhz;
    std::uint8_t vic;
    std::uint8_t pr;
};

inline constexpr std::size_t kNumVmodes = 15;
inline constexpr std::size_t kNumTvmodes = 4;

std::span<const PresetMode> vmodes() noexcept;
std::span<const PresetMode> tvmodes() noexcept;

Modeline calculate_cvt(std::uint32_t h_pixels, std::uint32_t v_lines, float refresh_rate,
                       std::uint8_t reduced_blanking, bool supports_pr);

inline constexpr std::size_t kYcMaxEntries = 20;

struct YcMode {
    std::array<char, 64> key{};
    std::int64_t phase_inc = 0;
    std::string_view key_view() const noexcept;
};

std::vector<YcMode> parse_yc(std::string_view text, std::string_view core_name);

std::optional<std::int64_t> yc_lookup(std::span<const YcMode> table, std::string_view key,
                                      std::string_view key_expanded);

enum class VFilter : std::uint8_t { Horz = 0, Vert = 1, Scanlines = 2, Ilace = 3 };

enum class VmodeFamily : std::uint8_t { Default = 0, Pal, Ntsc };

inline constexpr std::uint32_t kPalVtimeTicks = 1'800'000;

inline constexpr std::int64_t kScanlockMinVbp = 6;
inline constexpr std::int64_t kScanlockVtotalMax = 4095;
inline constexpr std::int64_t kScanlockRotMargin = 5;

inline constexpr double kScanlockVtotalRoundCeiling = static_cast<double>(kScanlockVtotalMax) + 0.5;

inline constexpr std::uint8_t kUioGetVres = 0x23;
inline constexpr std::uint8_t kUioSetHeight = 0x27;
inline constexpr std::uint8_t kUioSetFltCoef = 0x2A;
inline constexpr std::uint8_t kUioSetFltNum = 0x2B;
inline constexpr std::uint8_t kUioSetWidth = 0x37;
inline constexpr std::uint8_t kUioGetFbPar = 0x40;
inline constexpr std::uint8_t kUioSetYcPar = 0x41;

struct GeometryPass {
    VideoSample sample{};
    bool res_changed = false;
    bool fb_changed = false;
    bool changed = false;
    bool resent = false;
    bool yc_sent = false;
};

Ex<GeometryPass> sample_video_geometry(hal::ISpiTransport& link, bool force, GeometryGate& gate,
                                       IFilterResend* filters = nullptr);

class VideoService {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Wiring;

    VideoService(const VideoService&) = delete;
    VideoService& operator=(const VideoService&) = delete;
    explicit VideoService(Wiring w) noexcept;
    [[nodiscard]] static VideoService create(Wiring w);
    [[nodiscard]] static VideoService create();

    struct ParsedMode {
        Modeline mode{};
        ModeRequest how = ModeRequest::PresetIndex;

        std::uint32_t preset_index = 0;

        std::optional<PllBlock> raw_pll{};
    };
    Ex<ParsedMode> parse_mode(std::string_view spec) const;

    struct StoredMode {
        Modeline mode{};
        bool explicitly_requested = false;
        bool fully_custom = false;
    };
    static StoredMode store_mode(const ParsedMode& parsed, bool support_fhd, bool supports_pr);

    static StoredMode preset_fallback(bool support_fhd, bool supports_pr);

    Ex<void> apply(const Modeline& m);

    Ex<void> poll_resolution();

    Ex<void> poll_hotplug();

    EdidStore& edid() noexcept { return edid_; }
    CecStateMachine& cec() noexcept { return cec_; }

    class IVideoWireSink {
    public:
        virtual ~IVideoWireSink() = default;
        virtual Ex<void> publish(std::uint8_t opcode, std::span<const std::uint16_t> words) = 0;

    protected:
        IVideoWireSink() = default;
        IVideoWireSink(const IVideoWireSink&) = default;
        IVideoWireSink& operator=(const IVideoWireSink&) = default;
    };

    class IResolutionSampler {
    public:
        virtual ~IResolutionSampler() = default;
        virtual Ex<VideoSample> sample(bool force) = 0;

    protected:
        IResolutionSampler() = default;
        IResolutionSampler(const IResolutionSampler&) = default;
        IResolutionSampler& operator=(const IResolutionSampler&) = default;
    };

    struct Wiring {
        infra::OptRef<IVideoWireSink> sink{};
        infra::OptRef<IResolutionSampler> sampler{};
    };

    void set_adv7513_io(const adv7513::Io& io) noexcept { io_ = io; }

    void set_hdmi_capable(bool v) noexcept { hdmi_capable_ = v; }
    bool hdmi_capable() const noexcept { return hdmi_capable_; }

    void set_wire_options(const Modeline::WireOptions& o) noexcept {
        TASTY_SEAT_BODY(VideoService);
        wire_opt_ = o;
    }

    void set_supports_pr(bool v) noexcept { supports_pr_ = v; }
    bool supports_pr() const noexcept { return supports_pr_; }

    const VideoSample& info() const noexcept { return info_; }
    Generation resolution_generation() const noexcept { return res_gen_; }

    Ex<void> request_power(bool on);
    bool power_requested() const noexcept { return power_requested_; }

    bool init_owed() const noexcept { return init_owed_; }
    void clear_init_owed() noexcept { init_owed_ = false; }

    Generation audio_generation() const noexcept { return audio_gen_; }

    Generation hotplug_generation() const noexcept { return hpd_gen_; }
    bool sink_powered() const noexcept { return sink_powered_; }

    static VFilter select_vfilter(const VideoSample& s, std::span<const bool> slot_enabled);

    VFilter vfilter() const noexcept { return vfilter_; }

    void set_filter_slots(std::span<const bool> enabled) noexcept;

    static std::optional<double> estimate_fpix(const Modeline& m, std::uint32_t vtime,
                                               double refresh_min, double refresh_max);

    struct ScanrateLock {
        std::uint32_t vfp = 0;
        std::uint32_t vbp = 0;
        std::uint32_t vtotal = 0;
        double target_scanrate_hz = 0.0;
        double actual_scanrate_hz = 0.0;
    };

    [[nodiscard]] static std::optional<ScanrateLock> lock_scanrate(const Modeline& m,
                                                                   std::uint32_t vtime) noexcept;

    [[nodiscard]] static bool rotation_needs_buffered_scaler(
        const Modeline& m, const std::optional<ScanrateLock>& lock, const VideoSample& s) noexcept;

    struct FamilyChoice {
        VmodeFamily family = VmodeFamily::Default;
        bool adjustable = false;
    };
    [[nodiscard]] static FamilyChoice select_family(std::uint32_t vtime, bool vsync_adjust,
                                                    bool have_pal, bool have_ntsc) noexcept;

    void request_force_sample() noexcept { force_next_ = true; }

    void reset_resolution_gate() noexcept {
        last_res_ = 0;
        vi_seen_ = false;
        last_fb_crc_ = 0;
        last_flt_flags_ = 0;
        info_ = VideoSample{};
        force_next_ = true;
    }

    void set_pll_block(const PllBlock& b) noexcept { pll_ = b; }
    const PllBlock& pll_block() const noexcept { return pll_; }

private:
    Ex<void> apply_tmds_power(bool on);

    Ex<void> settle_and_publish();

    adv7513::Io io_{};
    EdidStore edid_{io_};
    CecStateMachine cec_{io_};

    IVideoWireSink* sink_ = nullptr;
    IResolutionSampler* sampler_ = nullptr;
    Modeline::WireOptions wire_opt_{};

    VideoSample info_{};
    Generation res_gen_{};
    Generation hpd_gen_{};
    Generation audio_gen_{};
    bool hdmi_capable_ = false;
    bool power_requested_ = true;
    bool init_owed_ = false;
    bool supports_pr_ = true;
    bool sink_powered_ = false;
    bool force_next_ = true;
    bool vi_seen_ = false;
    PllBlock pll_{};

    std::uint16_t last_res_ = 0;
    std::uint8_t last_fb_crc_ = 0;
    std::uint16_t last_flt_flags_ = 0;
    VFilter vfilter_ = VFilter::Horz;
    std::array<bool, 4> filter_slots_{};
};

}  // namespace mister::svc
