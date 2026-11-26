// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include "cores/config_blob.h"
#include "cores/config_codec.h"
#include "cores/minimig/minimig_config_layout.h"
#include "cores/minimig/minimig_ladder_bus_addr.h"
#include "cores/minimig/ext_cfg_word.h"
#include "proto/types.h"
#include "hal/link_halves.h"
#include "hal/types.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/fixed_str.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "svc/nic_link.h"

namespace mister::cores::minimig {

struct MinimigConfig {
    TASTY_SEAT_EXEMPT(component);

    using Image = std::array<std::byte, mm_cfg::kImageBytes>;

    using SummaryText = FixedStr<256, StrFit::Clip>;
    using InfoText = FixedStr<64, StrFit::Clip>;

    enum class Preset : std::uint8_t { Cd32, Cdtv, A500, A600, A1200, kCount };

    enum class Drive : std::uint8_t { Ide0, Ide1, Ide2, Ide3, Cd32, Cdtv, kCount };

    struct IdeUnits {
        std::byte units{};

        [[nodiscard]] constexpr IdeUnits with(Drive unit) const noexcept {
            return IdeUnits{units | (std::byte{1} << std::to_underlying(unit))};
        }

        [[nodiscard]] constexpr std::byte frame_bits() const noexcept { return units << 1; }

        friend constexpr bool operator==(IdeUnits, IdeUnits) = default;
    };

    Image image{};

    [[nodiscard]] static MinimigConfig defaults() noexcept;

    [[nodiscard]] static MinimigConfig from_file(std::span<const std::byte> file) noexcept;

    [[nodiscard]] static bool accepts(std::span<const std::byte> file) noexcept;

    [[nodiscard]] static FixedStr<kConfigPreviewChars, StrFit::Clip> preview(
        ConfigBlob blob
    ) noexcept;

    [[nodiscard]] std::string_view kickstart() const noexcept;
    [[nodiscard]] std::string_view ext_rom() const noexcept;

    void set_kickstart(std::string_view path) noexcept;

    void set_ext_rom(std::string_view path) noexcept;

    [[nodiscard]] hal::LinkWord cpu_word() const noexcept;
    [[nodiscard]] hal::LinkWord chipset_word() const noexcept;
    [[nodiscard]] hal::LinkWord floppy_word() const noexcept;
    [[nodiscard]] hal::LinkWord memory_word() const noexcept;
    [[nodiscard]] hal::LinkWord hdd_word(IdeUnits present) const noexcept;
    [[nodiscard]] hal::LinkWord video_word() const noexcept;
    [[nodiscard]] hal::LinkWord audio_word() const noexcept;

    [[nodiscard]] hal::LinkWord joy_word() const noexcept;

    [[nodiscard]] hal::LinkHalves ext_cfg_halves() const noexcept;

    [[nodiscard]] bool hrtmon() const noexcept;

    [[nodiscard]] svc::NicLink nic_link() const noexcept;
    [[nodiscard]] bool ide_enabled() const noexcept;
    [[nodiscard]] bool aga() const noexcept;
    [[nodiscard]] bool ntsc() const noexcept;

    [[nodiscard]] BusAddr chip_ram_top() const noexcept;

    [[nodiscard]] bool drive_named(Drive drive) const noexcept;

    [[nodiscard]] SummaryText summary_line() const noexcept;

    [[nodiscard]] InfoText info_line() const noexcept;
    void stamp_info() noexcept;

    void apply(Preset preset, proto::FileSize kickstart = {}) noexcept;

    enum class CpuModel : std::uint8_t { M68000, M68010, Reserved, M68020 };
    enum class ChipsetModel : std::uint8_t {
        OcsA500,
        OcsA1000,
        Ecs,
        Reserved3,
        Reserved4,
        Reserved5,
        Aga,
        Reserved7
    };
    enum class ChipRam : std::uint8_t { K512, M1, M1_5, M2 };
    enum class SlowRam : std::uint8_t { None, K512, M1, M1_5 };

    enum class FastRam : std::uint8_t { None, M2, M4, M8, Big4, Big5, Big6, Big7 };
    enum class JoyMode : std::uint8_t { Digital, Analog, Cd32, Analog3 };

    enum class UnitMode : std::uint8_t { Off, Fixed, Removable };
    enum class Scandoubler : std::uint8_t { Off, Hq2x, Crt25, Crt50, Crt75, Fx5, Fx6, Fx7 };
    enum class Aspect : std::uint8_t { Original, FullScreen, Arc1, Arc2 };
    enum class StereoMix : std::uint8_t { Mix0, Mix25, Mix50, Mix100 };

    [[nodiscard]] CpuModel cpu_model() const noexcept;
    void set_cpu_model(CpuModel model) noexcept;

    [[nodiscard]] bool cpu_wide() const noexcept;
    [[nodiscard]] bool dcache() const noexcept;
    void set_dcache(bool on) noexcept;
    [[nodiscard]] ChipsetModel chipset_model() const noexcept;

    void set_chipset_model(ChipsetModel model) noexcept;
    void set_ntsc(bool on) noexcept;
    [[nodiscard]] ChipRam chip_ram() const noexcept;
    void set_chip_ram(ChipRam size) noexcept;
    [[nodiscard]] SlowRam slow_ram() const noexcept;
    void set_slow_ram(SlowRam size) noexcept;
    [[nodiscard]] FastRam fast_ram() const noexcept;
    void set_fast_ram(FastRam size) noexcept;
    void set_hrtmon(bool on) noexcept;
    [[nodiscard]] bool joy_swap() const noexcept;
    void set_joy_swap(bool on) noexcept;
    [[nodiscard]] JoyMode joy_mode() const noexcept;
    void set_joy_mode(JoyMode mode) noexcept;

    [[nodiscard]] bool floppy_turbo() const noexcept;
    void set_floppy_turbo(bool on) noexcept;
    void set_ide_enabled(bool on) noexcept;
    [[nodiscard]] bool ide_slow() const noexcept;
    void set_ide_slow(bool on) noexcept;
    [[nodiscard]] UnitMode unit_mode(Drive unit) const noexcept;
    void set_unit_mode(Drive unit, UnitMode mode) noexcept;
    [[nodiscard]] Scandoubler scandoubler() const noexcept;
    void set_scandoubler(Scandoubler fx) noexcept;

    [[nodiscard]] bool blank_plus() const noexcept;
    void set_blank_plus(bool on) noexcept;
    [[nodiscard]] Aspect aspect() const noexcept;
    void set_aspect(Aspect ratio) noexcept;

    [[nodiscard]] StereoMix stereo_mix() const noexcept;
    void set_stereo_mix(StereoMix mix) noexcept;

    [[nodiscard]] ExtCfgWord ext_cfg() const noexcept;
    void set_ext_cfg(ExtCfgWord value) noexcept;

    [[nodiscard]] std::string_view cpu_name() const noexcept;
    [[nodiscard]] std::string_view chipset_name() const noexcept;

    [[nodiscard]] std::string_view fast_ram_label() const noexcept;
    [[nodiscard]] std::string_view joy_mode_name() const noexcept;

    [[nodiscard]] hal::LinkWord joy_swap_word() const noexcept;
    [[nodiscard]] hal::LinkWord joy_mode_word() const noexcept;

    friend bool operator==(const MinimigConfig&, const MinimigConfig&) = default;

private:
    [[nodiscard]] std::byte at_(infra::ByteOffset at) const noexcept;
    [[nodiscard]] std::byte& at_(infra::ByteOffset at) noexcept;
    [[nodiscard]] std::span<const std::byte> field_(
        infra::ByteOffset at,
        infra::ByteCount len
    ) const noexcept;
    [[nodiscard]] std::span<std::byte> field_(infra::ByteOffset at, infra::ByteCount len) noexcept;
    [[nodiscard]] hal::LinkWord half_(infra::ByteOffset at) const noexcept;
    [[nodiscard]] bool well_formed_() const noexcept;

    [[nodiscard]] std::string_view chip_ram_name_() const noexcept;

    [[nodiscard]] std::string_view fast_ram_name_() const noexcept;
    [[nodiscard]] std::string_view slow_ram_name_() const noexcept;
    [[nodiscard]] bool any_hard_disk_() const noexcept;
};

static_assert(
    infra::ByteCount{sizeof(MinimigConfig)} == mm_cfg::kBytes && alignof(MinimigConfig) == 1,
    "the value IS the image: it crosses seats and reaches the file as its bytes"
);
static_assert(std::is_trivially_copyable_v<MinimigConfig>);
static_assert(
    infra::ByteCount{MinimigConfig::InfoText::kBufSize} == mm_cfg::kInfoBytes,
    "the info line is cut to its field"
);
static_assert(
    sizeof(MinimigConfig) <= kMaxConfigValueBytes,
    "MAIN holds one decoded value in a kMaxConfigValueBytes buffer"
);

}
