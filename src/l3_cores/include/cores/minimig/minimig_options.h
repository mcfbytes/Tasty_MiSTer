// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/minimig/io_frame_sink.h"
#include "cores/minimig/io_frame_sink_op.h"
#include "cores/minimig/minimig_config.h"
#include "cores/option_row.h"
#include "cores/option_row_view.h"
#include "cores/types.h"
#include "hal/types.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores::minimig {

enum class MinimigOption : std::uint8_t {
    None = 0,
    JoySwap,
    Cpu,
    DCache,
    Chipset,
    ChipRam,
    FastRam,
    SlowRam,
    Kickstart,
    ExtRom,
    Hrtmon,
    JoyMode,
    FloppyTurbo,
    IdeEnable,
    FastIde,
    Unit0,
    Unit1,
    Unit2,
    Unit3,
    TvStandard,
    Scandoubler,
    VideoArea,
    Aspect,
    PixelClock,
    Scaling,
    RtgUpscale,
    StereoMix,
    AudioFilter,
    Model,
    PaulaOutput,
    PresetA500,
    PresetA600,
    PresetA1200,
    kCount,
};

[[nodiscard]] constexpr OptionTag tag_of(MinimigOption o) noexcept {
    return OptionTag{static_cast<std::uint8_t>(o)};
}

[[nodiscard]] constexpr std::optional<MinimigOption> option_of(OptionTag id) noexcept {
    if (id.v >= static_cast<std::uint8_t>(MinimigOption::kCount))
        return std::nullopt;
    return static_cast<MinimigOption>(id.v);
}

class MinimigOptions {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit MinimigOptions(std::span<const OptionRow> rows) noexcept : rows_(rows) {}

    [[nodiscard]] OptionRowView view(proto::OptionRowIndex row, const MinimigConfig& cfg) noexcept;
    [[nodiscard]] Ex<void> set(
        proto::OptionRowIndex row,
        proto::FieldValue choice,
        MinimigConfig& cfg,
        IoFrameSink& frames
    );

private:
    [[nodiscard]] std::optional<proto::FieldValue> current_(
        MinimigOption o,
        const MinimigConfig& cfg
    ) const noexcept;

    [[nodiscard]] bool dimmed_(MinimigOption o, const MinimigConfig& cfg) const noexcept;

    [[nodiscard]] std::string_view shown_(
        MinimigOption o,
        const OptionRow::CycleArm& cycle,
        proto::FieldValue place,
        const MinimigConfig& cfg
    ) const noexcept;
    [[nodiscard]] Ex<void> assign_(
        MinimigOption o,
        proto::FieldValue choice,
        MinimigConfig& cfg,
        IoFrameSink& frames
    );
    [[nodiscard]] Ex<void> apply_preset_(MinimigOption o, MinimigConfig& cfg);
    [[nodiscard]] Ex<void> send_(IoFrameSink& frames, FrameOp op, hal::SpiWord parm);
    [[nodiscard]] Ex<void> set_ext_(ExtCfgWord next, MinimigConfig& cfg, IoFrameSink& frames);
    [[nodiscard]] Ex<void> send_ext_(const MinimigConfig& cfg, IoFrameSink& frames);

    std::span<const OptionRow> rows_;
    FixedStr<kOptionTextChars + 1, StrFit::Clip> text_{};
};

}
