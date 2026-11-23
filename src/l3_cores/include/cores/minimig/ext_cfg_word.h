// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include "hal/reg_field.h"
#include "hal/spi_halves.h"

namespace mister::cores::minimig {

struct ExtCfgWord {
    enum class Scaling : std::uint8_t {
        Normal,
        VInteger,
        HvIntegerLess,
        HvIntegerMore,
        HvInteger,
        Code5,
        Code6,
        Code7
    };

    enum class AudioFilter : std::uint8_t { Auto, AutoOn, Off, On };

    std::uint32_t v = 0;

    [[nodiscard]] static constexpr ExtCfgWord of(hal::SpiHalves halves) noexcept {
        return halves.joined([](std::uint32_t whole) { return ExtCfgWord{whole}; });
    }

    [[nodiscard]] constexpr hal::SpiHalves halves() const noexcept { return hal::SpiHalves::of(v); }

    [[nodiscard]] constexpr bool pixel_adaptive() const noexcept { return has_(kPixelAdaptive); }

    [[nodiscard]] constexpr ExtCfgWord with_pixel_adaptive(bool on) const noexcept {
        return flagged_(kPixelAdaptive, on);
    }

    [[nodiscard]] constexpr Scaling scaling() const noexcept {
        return static_cast<Scaling>(kScaling.get(v));
    }

    [[nodiscard]] constexpr ExtCfgWord with_scaling(Scaling mode) const noexcept {
        return placed_(kScaling, std::to_underlying(mode));
    }

    [[nodiscard]] constexpr std::string_view scaling_name() const noexcept {
        return kScalingNames[std::to_underlying(scaling())];
    }

    [[nodiscard]] constexpr bool rtg_integer() const noexcept { return has_(kRtgInteger); }

    [[nodiscard]] constexpr ExtCfgWord with_rtg_integer(bool on) const noexcept {
        return flagged_(kRtgInteger, on);
    }

    [[nodiscard]] constexpr AudioFilter filter() const noexcept {
        return static_cast<AudioFilter>(kFilter.get(v));
    }

    [[nodiscard]] constexpr ExtCfgWord with_filter(AudioFilter mode) const noexcept {
        return placed_(kFilter, std::to_underlying(mode));
    }

    [[nodiscard]] constexpr bool model_1200() const noexcept { return has_(kModel1200); }

    [[nodiscard]] constexpr ExtCfgWord with_model_1200(bool on) const noexcept {
        return flagged_(kModel1200, on);
    }

    [[nodiscard]] constexpr bool paula_pwm() const noexcept { return has_(kPaulaPwm); }

    [[nodiscard]] constexpr ExtCfgWord with_paula_pwm(bool on) const noexcept {
        return flagged_(kPaulaPwm, on);
    }

    friend constexpr bool operator==(ExtCfgWord, ExtCfgWord) = default;

private:
    static constexpr hal::RegField<std::uint32_t> kPixelAdaptive{10, 1};
    static constexpr hal::RegField<std::uint32_t> kScaling{11, 7};
    static constexpr hal::RegField<std::uint32_t> kRtgInteger{14, 1};
    static constexpr hal::RegField<std::uint32_t> kFilter{15, 3};
    static constexpr hal::RegField<std::uint32_t> kModel1200{17, 1};
    static constexpr hal::RegField<std::uint32_t> kPaulaPwm{18, 1};
    static constexpr std::array<std::string_view, 8> kScalingNames{
        "Normal",
        "V-Integer",
        "HV-Integer-",
        "HV-Integer+",
        "HV-Integer",
        "???",
        "???",
        "???"
    };
    static_assert(kScalingNames.size() == kScaling.mask + 1u);

    [[nodiscard]] constexpr bool has_(hal::RegField<std::uint32_t> field) const noexcept {
        return field.test(v);
    }

    [[nodiscard]] constexpr ExtCfgWord flagged_(
        hal::RegField<std::uint32_t> field,
        bool on
    ) const noexcept {
        return ExtCfgWord{field.set(v, hal::RegField<std::uint32_t>::Value{on ? 1u : 0u})};
    }

    [[nodiscard]] constexpr ExtCfgWord placed_(
        hal::RegField<std::uint32_t> field,
        std::uint32_t code
    ) const noexcept {
        return ExtCfgWord{field.set(v, hal::RegField<std::uint32_t>::Value{code})};
    }
};

static_assert(
    ExtCfgWord{}.with_scaling(ExtCfgWord::Scaling::Code7).scaling() == ExtCfgWord::Scaling::Code7
    && ExtCfgWord{}.with_filter(ExtCfgWord::AudioFilter::On).filter() == ExtCfgWord::AudioFilter::On
);

}
