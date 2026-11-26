// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/minimig_options.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/minimig/minimig_option_names.h"
#include "hal/link_halves.h"
#include "infra/ascii.h"
#include "infra/byte_count.h"
#include "infra/indexed_array.h"
#include "infra/indexed_span.h"
#include "infra/span_cut.h"

namespace mister::cores::minimig {

namespace {

using C = MinimigConfig;
using D = MinimigConfig::Drive;
using Ext = ExtCfgWord;

constexpr std::array kCpuRing{C::CpuModel::M68000, C::CpuModel::M68010, C::CpuModel::M68020};
constexpr std::array kChipsetRing{
    C::ChipsetModel::OcsA500,
    C::ChipsetModel::OcsA1000,
    C::ChipsetModel::Ecs,
    C::ChipsetModel::Aga
};
constexpr std::array kChipRams{C::ChipRam::K512, C::ChipRam::M1, C::ChipRam::M1_5, C::ChipRam::M2};
constexpr std::array
    kSlowRams{C::SlowRam::None, C::SlowRam::K512, C::SlowRam::M1, C::SlowRam::M1_5};
constexpr std::array kFastRams{
    C::FastRam::None,
    C::FastRam::M2,
    C::FastRam::M4,
    C::FastRam::M8,
    C::FastRam::Big4,
    C::FastRam::Big5,
    C::FastRam::Big6,
    C::FastRam::Big7
};
constexpr std::array
    kJoyModes{C::JoyMode::Digital, C::JoyMode::Analog, C::JoyMode::Cd32, C::JoyMode::Analog3};
constexpr std::array kUnitModes{C::UnitMode::Off, C::UnitMode::Fixed, C::UnitMode::Removable};
constexpr std::array kScandoublers{
    C::Scandoubler::Off,
    C::Scandoubler::Hq2x,
    C::Scandoubler::Crt25,
    C::Scandoubler::Crt50,
    C::Scandoubler::Crt75,
    C::Scandoubler::Fx5,
    C::Scandoubler::Fx6,
    C::Scandoubler::Fx7
};
constexpr std::array
    kAspects{C::Aspect::Original, C::Aspect::FullScreen, C::Aspect::Arc1, C::Aspect::Arc2};
constexpr std::array kStereoMixes{
    C::StereoMix::Mix0,
    C::StereoMix::Mix25,
    C::StereoMix::Mix50,
    C::StereoMix::Mix100
};
constexpr std::array kScalings{
    Ext::Scaling::Normal,
    Ext::Scaling::VInteger,
    Ext::Scaling::HvIntegerLess,
    Ext::Scaling::HvIntegerMore,
    Ext::Scaling::HvInteger,
    Ext::Scaling::Code5,
    Ext::Scaling::Code6,
    Ext::Scaling::Code7
};
constexpr std::array kFilterRing{
    Ext::AudioFilter::Auto,
    Ext::AudioFilter::Off,
    Ext::AudioFilter::On
};

static_assert(
    kCpuRing.size() == kCpuNames.size()
    && kChipsetRing.size() == kChipsetNames.size()
    && kChipRams.size() == kChipRamNames.size()
    && kSlowRams.size() == kSlowRamNames.size()
    && kFastRams.size() >= kFastRamNames.size()
    && kJoyModes.size() >= kJoyModeNames.size()
    && kUnitModes.size() == kUnitNames.size()
    && kScandoublers.size() >= kScandoublerNames.size()
    && kAspects.size() == kAspectNames.size()
    && kStereoMixes.size() == kStereoNames.size()
    && kScalings.size() >= kScalingNames.size()
    && kFilterRing.size() == kFilterNames.size()
);

constexpr proto::FieldCount kNarrowFastReach{4};
constexpr auto kFastReach = proto::FieldCount::saturating(kFastRamNames.size());
constexpr std::string_view kNoExtRom = "<none>";
constexpr infra::ByteCount kRomNameChars{20};

constexpr std::string_view kHomePrefix = "games/Amiga/";

template <class T, std::size_t N>
[[nodiscard]] constexpr proto::FieldValue place_of(const std::array<T, N>& ring, T value) noexcept {
    constexpr auto kPlaces = proto::FieldCount::saturating(N);
    proto::FieldValue at{};
    for (const T& code : ring) {
        if (code == value)
            return at;
        at = at.cycled_forward(kPlaces);
    }
    return proto::FieldValue{};
}

template <class T, std::size_t N>
[[nodiscard]] constexpr T code_at(const std::array<T, N>& ring, proto::FieldValue place) noexcept {
    return infra::at_index(ring, place);
}

[[nodiscard]] constexpr std::optional<C::Preset> preset_of(MinimigOption o) noexcept {
    if (o == MinimigOption::PresetA500)
        return C::Preset::A500;
    if (o == MinimigOption::PresetA600)
        return C::Preset::A600;
    if (o == MinimigOption::PresetA1200)
        return C::Preset::A1200;
    return std::nullopt;
}

[[nodiscard]] std::string_view rom_name(std::string_view path) noexcept {
    if (infra::istarts_with(path, kHomePrefix))
        path.remove_prefix(kHomePrefix.size());
    return infra::affine::first_clamped(path, kRomNameChars);
}

[[nodiscard]] constexpr D unit_of(MinimigOption o) noexcept {
    if (o == MinimigOption::Unit1)
        return D::Ide1;
    if (o == MinimigOption::Unit2)
        return D::Ide2;
    if (o == MinimigOption::Unit3)
        return D::Ide3;
    return D::Ide0;
}

[[nodiscard]] constexpr bool is_unit(MinimigOption o) noexcept {
    return o == MinimigOption::Unit0
        || o == MinimigOption::Unit1
        || o == MinimigOption::Unit2
        || o == MinimigOption::Unit3;
}

}

std::optional<proto::FieldValue> MinimigOptions::current_(
    MinimigOption o,
    const MinimigConfig& cfg
) const noexcept {
    const ExtCfgWord ext = cfg.ext_cfg();
    switch (o) {
        case MinimigOption::JoySwap:
            return proto::FieldValue::flag(cfg.joy_swap());
        case MinimigOption::Cpu:
            return place_of(kCpuRing, cfg.cpu_model());
        case MinimigOption::DCache:
            return proto::FieldValue::flag(cfg.dcache());
        case MinimigOption::Chipset:
            return place_of(kChipsetRing, cfg.chipset_model());
        case MinimigOption::ChipRam:
            return place_of(kChipRams, cfg.chip_ram());
        case MinimigOption::FastRam:
            return place_of(kFastRams, cfg.fast_ram());
        case MinimigOption::SlowRam:
            return place_of(kSlowRams, cfg.slow_ram());
        case MinimigOption::Hrtmon:
            return proto::FieldValue::flag(cfg.hrtmon());
        case MinimigOption::JoyMode:
            return place_of(kJoyModes, cfg.joy_mode());
        case MinimigOption::FloppyTurbo:
            return proto::FieldValue::flag(cfg.floppy_turbo());
        case MinimigOption::IdeEnable:
            return proto::FieldValue::flag(cfg.ide_enabled());
        case MinimigOption::FastIde:
            return proto::FieldValue::flag(cfg.ide_slow());
        case MinimigOption::Unit0:
        case MinimigOption::Unit1:
        case MinimigOption::Unit2:
        case MinimigOption::Unit3:
            return place_of(kUnitModes, cfg.unit_mode(unit_of(o)));
        case MinimigOption::TvStandard:
            return proto::FieldValue::flag(cfg.ntsc());
        case MinimigOption::Scandoubler:
            return place_of(kScandoublers, cfg.scandoubler());
        case MinimigOption::VideoArea:
            return proto::FieldValue::flag(cfg.blank_plus());
        case MinimigOption::Aspect:
            return place_of(kAspects, cfg.aspect());
        case MinimigOption::PixelClock:
            return proto::FieldValue::flag(ext.pixel_adaptive());
        case MinimigOption::Scaling:
            return place_of(kScalings, ext.scaling());
        case MinimigOption::RtgUpscale:
            return proto::FieldValue::flag(ext.rtg_integer());
        case MinimigOption::StereoMix:
            return place_of(kStereoMixes, cfg.stereo_mix());
        case MinimigOption::AudioFilter:
            return place_of(kFilterRing, ext.filter());
        case MinimigOption::Model:
            return proto::FieldValue::flag(ext.model_1200());
        case MinimigOption::PaulaOutput:
            return proto::FieldValue::flag(ext.paula_pwm());
        case MinimigOption::None:
        case MinimigOption::Kickstart:
        case MinimigOption::ExtRom:
        case MinimigOption::PresetA500:
        case MinimigOption::PresetA600:
        case MinimigOption::PresetA1200:
        case MinimigOption::kCount:
            break;
    }
    return std::nullopt;
}

bool MinimigOptions::dimmed_(MinimigOption o, const MinimigConfig& cfg) const noexcept {
    if (o == MinimigOption::DCache)
        return !cfg.cpu_wide();
    if (o == MinimigOption::FastIde)
        return !cfg.ide_enabled() || !cfg.cpu_wide();
    if (is_unit(o))
        return !cfg.ide_enabled();
    return false;
}

OptionRowView MinimigOptions::view(proto::OptionRowIndex index, const MinimigConfig& cfg) noexcept {
    const infra::IndexedSpan<const OptionRow, proto::OptionRowIndex> rows{rows_};
    const auto found = rows.find(index);
    if (!found)
        return {};
    const auto o = option_of(found->id);
    if (!o)
        return {};
    if (*o == MinimigOption::Kickstart || *o == MinimigOption::ExtRom) {
        const std::string_view path = *o == MinimigOption::Kickstart ? cfg.kickstart()
                                                                     : cfg.ext_rom();
        (void)text_.assign(
            path.empty() && *o == MinimigOption::ExtRom ? kNoExtRom : rom_name(path)
        );
        return OptionRowView{
            .current = path.empty() ? std::uint8_t{0} : std::uint8_t{1},
            .dimmed = false,
            .text = text_.view()
        };
    }
    const OptionRow::CycleArm* cycle = found->cycle_arm();
    const std::optional<proto::FieldValue> cur = current_(*o, cfg);
    if (cycle == nullptr || !cur)
        return {};
    (void)text_.assign(shown_(*o, *cycle, *cur, cfg));
    OptionRowView view = OptionRowView::cycle(*cur, text_.view());
    view.dimmed = dimmed_(*o, cfg);
    return view;
}

std::string_view MinimigOptions::shown_(
    MinimigOption o,
    const OptionRow::CycleArm& cycle,
    proto::FieldValue place,
    const MinimigConfig& cfg
) const noexcept {
    if (o == MinimigOption::Cpu)
        return cfg.cpu_name();
    if (o == MinimigOption::Chipset)
        return cfg.chipset_name();
    if (o == MinimigOption::FastRam)
        return cfg.fast_ram_label();
    if (o == MinimigOption::JoyMode)
        return cfg.joy_mode_name();
    if (o == MinimigOption::Scaling)
        return cfg.ext_cfg().scaling_name();

    if (o == MinimigOption::Aspect)
        return {};
    return cycle.value_name(place);
}

Ex<void> MinimigOptions::set(
    proto::OptionRowIndex index,
    proto::FieldValue choice,
    MinimigConfig& cfg,
    IoFrameSink& frames
) {
    const infra::IndexedSpan<const OptionRow, proto::OptionRowIndex> rows{rows_};
    const auto found = rows.find(index);
    if (!found)
        return std::unexpected(index.refusal(Errc::slot_range, ERR_SITE()));
    const auto o = option_of(found->id);
    if (!o)
        return std::unexpected(index.refusal(Errc::slot_range, ERR_SITE()));
    if (const OptionRow::ActionArm* act = found->action_arm(); act != nullptr && act->core_act)
        return apply_preset_(*o, cfg);
    const OptionRow::CycleArm* cycle = found->cycle_arm();
    if (cycle == nullptr || !current_(*o, cfg) || !choice.within(cycle->choice_count())) {
        return std::unexpected(index.refusal(Errc::slot_range, ERR_SITE()));
    }
    if (dimmed_(*o, cfg))
        return {};
    return assign_(*o, choice, cfg, frames);
}

Ex<void> MinimigOptions::apply_preset_(MinimigOption o, MinimigConfig& cfg) {
    const std::optional<MinimigConfig::Preset> preset = preset_of(o);
    if (!preset)
        return std::unexpected(tag_of(o).refusal(Errc::slot_range, ERR_SITE()));
    cfg.apply(*preset);
    return {};
}

Ex<void> MinimigOptions::assign_(
    MinimigOption o,
    proto::FieldValue choice,
    MinimigConfig& cfg,
    IoFrameSink& frames
) {
    const bool on = choice != proto::FieldValue{};
    const ExtCfgWord ext = cfg.ext_cfg();
    switch (o) {
        case MinimigOption::JoySwap:
            cfg.set_joy_swap(on);
            return send_(frames, FrameOp::Joy, cfg.joy_swap_word());
        case MinimigOption::Cpu:
            cfg.set_cpu_model(code_at(kCpuRing, choice));
            return send_(frames, FrameOp::Cpu, cfg.cpu_word());
        case MinimigOption::DCache:
            cfg.set_dcache(on);
            return send_(frames, FrameOp::Cpu, cfg.cpu_word());
        case MinimigOption::Chipset:
            cfg.set_chipset_model(code_at(kChipsetRing, choice));
            return send_(frames, FrameOp::Chipset, cfg.chipset_word());
        case MinimigOption::ChipRam:
            cfg.set_chip_ram(code_at(kChipRams, choice));
            return {};
        case MinimigOption::FastRam: {

            const proto::FieldValue was = place_of(kFastRams, cfg.fast_ram());
            const bool forward = choice == was.cycled_forward(kFastReach);
            C::FastRam next = code_at(kFastRams, choice);
            if (!cfg.cpu_wide() && !choice.within(kNarrowFastReach))
                next = forward ? C::FastRam::None : C::FastRam::M8;
            cfg.set_fast_ram(next);
            return {};
        }
        case MinimigOption::SlowRam:
            cfg.set_slow_ram(code_at(kSlowRams, choice));
            return {};
        case MinimigOption::Hrtmon:
            cfg.set_hrtmon(on);
            return {};
        case MinimigOption::JoyMode:
            cfg.set_joy_mode(code_at(kJoyModes, choice));
            return send_(frames, FrameOp::Joy, cfg.joy_mode_word());
        case MinimigOption::FloppyTurbo:
            cfg.set_floppy_turbo(on);
            return send_(frames, FrameOp::Floppy, cfg.floppy_word());
        case MinimigOption::IdeEnable:
            cfg.set_ide_enabled(on);
            return {};
        case MinimigOption::FastIde:
            cfg.set_ide_slow(on);
            return {};
        case MinimigOption::Unit0:
        case MinimigOption::Unit1:
        case MinimigOption::Unit2:
        case MinimigOption::Unit3:
            cfg.set_unit_mode(unit_of(o), code_at(kUnitModes, choice));
            return {};
        case MinimigOption::TvStandard:
            cfg.set_ntsc(on);
            return send_(frames, FrameOp::Chipset, cfg.chipset_word());
        case MinimigOption::Scandoubler:
            cfg.set_scandoubler(code_at(kScandoublers, choice));
            return send_(frames, FrameOp::Video, cfg.video_word());
        case MinimigOption::VideoArea:
            cfg.set_blank_plus(on);
            return send_(frames, FrameOp::Video, cfg.video_word());
        case MinimigOption::Aspect:
            cfg.set_aspect(code_at(kAspects, choice));
            return send_(frames, FrameOp::Video, cfg.video_word());
        case MinimigOption::PixelClock:
            return set_ext_(ext.with_pixel_adaptive(on), cfg, frames);
        case MinimigOption::Scaling:
            return set_ext_(ext.with_scaling(code_at(kScalings, choice)), cfg, frames);
        case MinimigOption::RtgUpscale:
            return set_ext_(ext.with_rtg_integer(on), cfg, frames);
        case MinimigOption::StereoMix:
            cfg.set_stereo_mix(code_at(kStereoMixes, choice));
            return send_(frames, FrameOp::Audio, cfg.audio_word());
        case MinimigOption::AudioFilter:
            return set_ext_(ext.with_filter(code_at(kFilterRing, choice)), cfg, frames);
        case MinimigOption::Model:
            return set_ext_(ext.with_model_1200(on), cfg, frames);
        case MinimigOption::PaulaOutput:
            return set_ext_(ext.with_paula_pwm(on), cfg, frames);
        case MinimigOption::None:
        case MinimigOption::Kickstart:
        case MinimigOption::ExtRom:
        case MinimigOption::PresetA500:
        case MinimigOption::PresetA600:
        case MinimigOption::PresetA1200:
        case MinimigOption::kCount:
            break;
    }
    return std::unexpected(tag_of(o).refusal(Errc::slot_range, ERR_SITE()));
}

Ex<void> MinimigOptions::send_(IoFrameSink& frames, FrameOp op, hal::LinkWord parm) {
    std::array<std::uint8_t, 2 * sizeof(hal::LinkWord)> bytes{};
    word(op).store_le(std::span(bytes).first<sizeof(hal::LinkWord)>());
    parm.store_le(std::span(bytes).last<sizeof(hal::LinkWord)>());
    return frames.write(bytes);
}

Ex<void> MinimigOptions::set_ext_(ExtCfgWord next, MinimigConfig& cfg, IoFrameSink& frames) {
    cfg.set_ext_cfg(next);
    return send_ext_(cfg, frames);
}

Ex<void> MinimigOptions::send_ext_(const MinimigConfig& cfg, IoFrameSink& frames) {
    const hal::LinkHalves halves = cfg.ext_cfg().halves();
    std::array<std::uint8_t, 5 * sizeof(hal::LinkWord)> bytes{};
    const std::span<std::uint8_t> out{bytes};
    word(FrameOp::Status).store_le(out.first<sizeof(hal::LinkWord)>());
    halves.low.store_le(out.subspan<3 * sizeof(hal::LinkWord), sizeof(hal::LinkWord)>());
    halves.high.store_le(out.subspan<4 * sizeof(hal::LinkWord), sizeof(hal::LinkWord)>());
    return frames.write(bytes);
}

}
