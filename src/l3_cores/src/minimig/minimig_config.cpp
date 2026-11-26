// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/minimig_config.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "infra/byte_view.h"
#include "infra/indexed_array.h"
#include "infra/path_parts.h"
#include "infra/span_cut.h"

namespace mister::cores::minimig {

namespace {

using infra::ByteCount;
using infra::ByteOffset;

constexpr std::byte kZero{0};

constexpr std::byte kCpuModelBits{0x03};
constexpr std::byte kCpuFastTableBit{0x02};
constexpr std::byte kChipsetNtsc{0x02};
constexpr std::byte kChipsetAga{0x10};
constexpr std::byte kChipsetNameBits{0x07};
constexpr std::byte kPairBits{0x03};

constexpr std::byte kFastRamHighCode{0x04};
constexpr std::byte kMemoryHrtmon{0x40};
constexpr std::byte kIdeEnable{0x01};
constexpr std::byte kIdeSlow{0x20};
constexpr std::byte kCpuCache{0x10};
constexpr std::byte kChipsetKeptBits{0x03};
constexpr std::byte kFastCodeBits{0x07};
constexpr std::byte kJoySwapBit{0x08};
constexpr std::byte kUnitModeBits{0x03};
constexpr std::byte kAreaPlus{0x01};
constexpr std::byte kFloppyFast{1};
constexpr std::byte kOneFloppy{1};
constexpr std::byte kMaxFloppyDrives{4};
constexpr std::byte kDriveOn{1};
constexpr std::byte kApplyAutofireMask{0x0C};

constexpr std::byte kCpuWordBits{0x1F};
constexpr std::byte kChipsetWordBits{0x3F};
constexpr std::byte kChipsetCdtvBit{0x20};
constexpr std::byte kIdeWordBits{0x21};
constexpr std::byte kScanlineBits{0x07};
constexpr hal::LinkWord kMt32Reset{0x1};
constexpr std::byte kDefaultMemory{0x11};

constexpr BusBytes kChipRamStep{512u * 1024u};

constexpr ByteCount kCpuNameSkip{2};

struct ByteField {
    std::byte bits{};
    unsigned shift = 0;

    template <class Code>
    [[nodiscard]] constexpr Code in(std::byte b) const noexcept {
        return static_cast<Code>(std::to_integer<std::uint8_t>((b >> shift) & bits));
    }

    template <class Code>
    [[nodiscard]] constexpr std::byte into(std::byte b, Code code) const noexcept {
        const std::byte value{std::to_underlying(code)};
        return (b & ~(bits << shift)) | ((value & bits) << shift);
    }

    [[nodiscard]] constexpr std::byte code(std::byte b) const noexcept {
        return (b >> shift) & bits;
    }

    [[nodiscard]] constexpr std::byte placed(std::byte code_) const noexcept {
        return (code_ & bits) << shift;
    }
};

struct ByteFlag {
    std::byte bit{};

    [[nodiscard]] constexpr bool in(std::byte b) const noexcept { return (b & bit) != kZero; }

    [[nodiscard]] constexpr std::byte into(std::byte b, bool on) const noexcept {
        return on ? (b | bit) : (b & ~bit);
    }
};

constexpr ByteField kCpuModelField{.bits = kCpuModelBits};
constexpr ByteField kChipsetModelField{.bits = kChipsetNameBits, .shift = 2};
constexpr ByteField kChipRamField{.bits = kPairBits};
constexpr ByteField kSlowRamField{.bits = kPairBits, .shift = 2};
constexpr ByteField kFastCodeField{.bits = kFastCodeBits};
constexpr ByteField kFastSplitField{.bits = kPairBits, .shift = 4};
constexpr ByteField kFastHighField{.bits = kFastRamHighCode, .shift = 5};
constexpr ByteField kJoyModeField{.bits = kPairBits, .shift = 1};
constexpr ByteField kUnitModeField{.bits = kUnitModeBits};
constexpr ByteField kScandoublerField{.bits = kScanlineBits};
constexpr ByteField kAreaField{.bits = kPairBits, .shift = 6};
constexpr ByteField kAspectField{.bits = kPairBits, .shift = 4};
constexpr ByteField kStereoField{.bits = kPairBits};
constexpr ByteField kFloppyDrivesField{.bits = kPairBits, .shift = 2};
constexpr ByteField kVideoAreaField{.bits = kPairBits, .shift = 2};
constexpr ByteField kVideoAspectField{.bits = kPairBits, .shift = 0};
constexpr ByteFlag kCpuWideFlag{kCpuFastTableBit};
constexpr ByteFlag kCpuCacheFlag{kCpuCache};
constexpr ByteFlag kNtscFlag{kChipsetNtsc};
constexpr ByteFlag kHrtmonFlag{kMemoryHrtmon};
constexpr ByteFlag kJoySwapFlag{kJoySwapBit};
constexpr ByteFlag kFloppyFastFlag{kFloppyFast};
constexpr ByteFlag kIdeEnableFlag{kIdeEnable};
constexpr ByteFlag kIdeSlowFlag{kIdeSlow};
constexpr ByteFlag kBlankPlusFlag{kAreaField.placed(kAreaPlus)};

constexpr std::array<std::string_view, 4> kChipNames = {"512K", "1M", "1.5M", "2M"};
constexpr std::array<std::string_view, 4> kSlowNames = {"none", "512K", "1M", "1.5M"};
constexpr std::array<std::string_view, 8> kPlainFastNames =
    {"none", "2M", "4M", "8M", "8M", "8M", "8M", "8M"};
constexpr std::array<std::string_view, 8> kWideFastNames =
    {"none", "2M", "4M", "8M", "256M", "384M", "256M", "256M"};
constexpr std::array<std::string_view, 4> kCpuNames = {"68000", "68010", "-----", "68020"};
constexpr std::array<std::string_view, 8> kChipsetNames =
    {"OCS-A500", "OCS-A1000", "ECS", "---", "---", "---", "AGA", "---"};
constexpr std::array<std::string_view, 4> kJoyModeNames = {"Digital", "Analog", "CD32", "Analog"};
constexpr std::string_view kNoneFitted = "none";

using C = MinimigConfig;
static_assert(
    kCpuNames.size() == std::to_integer<std::size_t>(kCpuModelField.bits) + 1u
    && kChipsetNames.size() == std::to_integer<std::size_t>(kChipsetModelField.bits) + 1u
    && kChipNames.size() == std::to_integer<std::size_t>(kChipRamField.bits) + 1u
    && kSlowNames.size() == std::to_integer<std::size_t>(kSlowRamField.bits) + 1u
    && kWideFastNames.size() == std::to_integer<std::size_t>(kFastCodeField.bits) + 1u
    && kPlainFastNames.size() == kWideFastNames.size()
    && kJoyModeNames.size() == std::to_integer<std::size_t>(kJoyModeField.bits) + 1u
);
static_assert(
    kCpuModelField.in<C::CpuModel>(kCpuModelField.into(std::byte{}, C::CpuModel::M68020))
        == C::CpuModel::M68020
    && kChipsetModelField.in<C::ChipsetModel>(
           kChipsetModelField.into(std::byte{}, C::ChipsetModel::Reserved7)
       ) == C::ChipsetModel::Reserved7
    && kChipRamField.in<C::ChipRam>(kChipRamField.into(std::byte{}, C::ChipRam::M2))
        == C::ChipRam::M2
    && kSlowRamField.in<C::SlowRam>(kSlowRamField.into(std::byte{}, C::SlowRam::M1_5))
        == C::SlowRam::M1_5
    && kFastCodeField.in<C::FastRam>(kFastCodeField.into(std::byte{}, C::FastRam::Big7))
        == C::FastRam::Big7
    && kJoyModeField.in<C::JoyMode>(kJoyModeField.into(std::byte{}, C::JoyMode::Analog3))
        == C::JoyMode::Analog3
);

struct PresetRow {
    std::byte cpu{};
    std::byte chipset{};
    std::byte memory{};
    std::string_view kickstart;
    std::byte autofire{};
    std::optional<MinimigConfig::Drive> drive{};
    std::string_view ext_rom{};
};

constexpr proto::FileSize kWholeKickstart{1024u * 1024u};
constexpr PresetRow kCd32{
    .cpu = std::byte{3},
    .chipset = kChipsetModelField.placed(std::byte{6}),
    .memory = std::byte{3},
    .kickstart = "Games/Amiga/CD32.rom",
    .autofire = std::byte{2} << 1,
    .drive = MinimigConfig::Drive::Cd32,
    .ext_rom = "Games/Amiga/CD32_ext.rom"
};
constexpr PresetRow kCdtv{
    .chipset = kChipsetModelField.placed(std::byte{2}),
    .memory = std::byte{1},
    .kickstart = "Games/Amiga/CDTV.rom",
    .drive = MinimigConfig::Drive::Cdtv,
    .ext_rom = "Games/Amiga/CDTV_ext.rom"
};
constexpr PresetRow kA500{.kickstart = "Games/Amiga/a500.rom"};
constexpr PresetRow kA600{
    .chipset = kChipsetModelField.placed(std::byte{2}),
    .memory = std::byte{1},
    .kickstart = "Games/Amiga/a600.rom"
};
constexpr PresetRow kA1200{
    .cpu = std::byte{3},
    .chipset = kChipsetModelField.placed(std::byte{6}),
    .memory = std::byte{3},
    .kickstart = "Games/Amiga/a1200.rom"
};

constexpr std::string_view kNoInfo = "NO INFO";

constexpr hal::LinkWord low_word(std::byte low) noexcept {
    return hal::LinkWord::load_le(std::array{low, kZero});
}

constexpr ByteOffset record_of(MinimigConfig::Drive drive) noexcept {
    using D = MinimigConfig::Drive;
    switch (drive) {
        case D::Ide0:
            return mm_cfg::kHardfile0;
        case D::Ide1:
            return mm_cfg::kHardfile1;
        case D::Ide2:
            return mm_cfg::kHardfile2;
        case D::Ide3:
            return mm_cfg::kHardfile3;
        case D::Cd32:
            return mm_cfg::kCd32Drive;
        case D::Cdtv:
            return mm_cfg::kCdtvDrive;
        case D::kCount:
            break;
    }
    return mm_cfg::kHardfile0;
}

void put_text(std::span<std::byte> dst, std::string_view text) noexcept {
    const std::string_view kept = text.substr(0, dst.size() - 1);
    std::ranges::fill(
        std::ranges::copy(infra::text_bytes(kept), dst.begin()).out,
        dst.end(),
        kZero
    );
}

std::optional<MinimigConfig> overlay(std::span<const std::byte> file) noexcept {
    MinimigConfig c{};
    if (file.empty() || file.size() > c.image.size())
        return std::nullopt;
    if (ByteCount{file.size()} == mm_cfg::kOldBytes) {
        std::ranges::copy(infra::affine::first(file, mm_cfg::kOldCpu), c.image.begin());
        infra::at_index(c.image, mm_cfg::kCpu) = infra::at_index(file, mm_cfg::kOldCpu);
        infra::at_index(c.image, mm_cfg::kAutofire) = infra::at_index(file, mm_cfg::kOldAutofire);
    } else {
        std::ranges::copy(file, c.image.begin());
    }
    return c;
}

}

std::byte MinimigConfig::at_(ByteOffset at) const noexcept {
    return infra::at_index(image, at);
}

std::byte& MinimigConfig::at_(ByteOffset at) noexcept {
    return infra::at_index(image, at);
}

std::span<const std::byte> MinimigConfig::field_(ByteOffset at, ByteCount len) const noexcept {
    return infra::affine::first(infra::affine::from(std::span<const std::byte>(image), at), len);
}

std::span<std::byte> MinimigConfig::field_(ByteOffset at, ByteCount len) noexcept {
    return infra::affine::first(infra::affine::from(std::span<std::byte>(image), at), len);
}

hal::LinkWord MinimigConfig::half_(ByteOffset at) const noexcept {
    return hal::LinkWord::load_le(field_(at, mm_cfg::kHalf).first<sizeof(hal::LinkWord)>());
}

bool MinimigConfig::well_formed_() const noexcept {
    const std::string_view id = infra::as_text(field_(mm_cfg::kId, mm_cfg::kIdBytes));
    return id == mm_cfg::kFileId && at_(mm_cfg::kFloppyDrives) <= kMaxFloppyDrives;
}

MinimigConfig MinimigConfig::defaults() noexcept {
    MinimigConfig c{};
    std::ranges::copy(
        infra::text_bytes(mm_cfg::kFileId),
        c.field_(mm_cfg::kId, mm_cfg::kIdBytes).begin()
    );
    c.set_kickstart(mm_cfg::kDefaultKickstart);
    c.at_(mm_cfg::kMemory) = kDefaultMemory;
    c.at_(mm_cfg::kFloppySpeed) = kFloppyFast;
    c.at_(mm_cfg::kFloppyDrives) = kOneFloppy;
    c.at_(record_of(Drive::Ide0)) = kDriveOn;
    c.at_(record_of(Drive::Ide1)) = kDriveOn;
    return c;
}

MinimigConfig MinimigConfig::from_file(std::span<const std::byte> file) noexcept {
    const std::optional<MinimigConfig> c = overlay(file);
    return c && c->well_formed_() ? *c : defaults();
}

bool MinimigConfig::accepts(std::span<const std::byte> file) noexcept {
    const std::optional<MinimigConfig> c = overlay(file);
    return c && c->well_formed_();
}

FixedStr<kConfigPreviewChars, StrFit::Clip> MinimigConfig::preview(ConfigBlob blob) noexcept {
    FixedStr<kConfigPreviewChars, StrFit::Clip> out{};
    if (blob.bytes.empty())
        return out;

    MinimigConfig c{};
    std::ranges::copy(
        blob.bytes.first(std::min(blob.bytes.size(), c.image.size())),
        c.image.begin()
    );
    const bool field_ends = c.field_(mm_cfg::kKickstart, mm_cfg::kKickstartBytes).back() == kZero;
    const std::string_view label = infra::text_until_nul(
        c.field_(mm_cfg::kLabel, mm_cfg::kLabelBytes)
    );
    std::string_view line = infra::text_until_nul(c.field_(mm_cfg::kInfo, mm_cfg::kInfoBytes));
    if (field_ends && !label.empty())
        line = label;
    line = line.substr(0, line.find('\n'));
    (void)out.assign(line.empty() ? kNoInfo : line);
    return out;
}

std::string_view MinimigConfig::kickstart() const noexcept {
    return infra::text_until_nul(field_(mm_cfg::kKickstart, mm_cfg::kKickstartBytes));
}

std::string_view MinimigConfig::ext_rom() const noexcept {

    const ByteCount behind = ByteCount{kickstart().size()} + mm_cfg::kByte;
    if (behind + mm_cfg::kByte >= mm_cfg::kKickstartBytes)
        return {};
    return infra::text_until_nul(
        infra::affine::from(field_(mm_cfg::kKickstart, mm_cfg::kKickstartBytes), behind)
    );
}

void MinimigConfig::set_kickstart(std::string_view path) noexcept {
    put_text(field_(mm_cfg::kKickstart, mm_cfg::kKickstartBytes), path);
}

void MinimigConfig::set_ext_rom(std::string_view path) noexcept {
    const ByteCount behind = ByteCount{kickstart().size()} + mm_cfg::kByte;
    if (behind + mm_cfg::kByte >= mm_cfg::kKickstartBytes)
        return;
    put_text(
        infra::affine::from(field_(mm_cfg::kKickstart, mm_cfg::kKickstartBytes), behind),
        path
    );
}

MinimigConfig::CpuModel MinimigConfig::cpu_model() const noexcept {
    return kCpuModelField.in<CpuModel>(at_(mm_cfg::kCpu));
}

void MinimigConfig::set_cpu_model(CpuModel model) noexcept {
    at_(mm_cfg::kCpu) = kCpuModelField.into(at_(mm_cfg::kCpu), model);
}

bool MinimigConfig::cpu_wide() const noexcept {
    return kCpuWideFlag.in(at_(mm_cfg::kCpu));
}

bool MinimigConfig::dcache() const noexcept {
    return kCpuCacheFlag.in(at_(mm_cfg::kCpu));
}

void MinimigConfig::set_dcache(bool on) noexcept {
    at_(mm_cfg::kCpu) = kCpuCacheFlag.into(at_(mm_cfg::kCpu), on);
}

MinimigConfig::ChipsetModel MinimigConfig::chipset_model() const noexcept {
    return kChipsetModelField.in<ChipsetModel>(at_(mm_cfg::kChipset));
}

void MinimigConfig::set_chipset_model(ChipsetModel model) noexcept {
    const std::byte kept = at_(mm_cfg::kChipset) & kChipsetKeptBits;
    at_(mm_cfg::kChipset) = kChipsetModelField.into(kept, model);
}

void MinimigConfig::set_ntsc(bool on) noexcept {
    at_(mm_cfg::kChipset) = kNtscFlag.into(at_(mm_cfg::kChipset), on);
}

MinimigConfig::ChipRam MinimigConfig::chip_ram() const noexcept {
    return kChipRamField.in<ChipRam>(at_(mm_cfg::kMemory));
}

void MinimigConfig::set_chip_ram(ChipRam size) noexcept {
    at_(mm_cfg::kMemory) = kChipRamField.into(at_(mm_cfg::kMemory), size);
}

MinimigConfig::SlowRam MinimigConfig::slow_ram() const noexcept {
    return kSlowRamField.in<SlowRam>(at_(mm_cfg::kMemory));
}

void MinimigConfig::set_slow_ram(SlowRam size) noexcept {
    at_(mm_cfg::kMemory) = kSlowRamField.into(at_(mm_cfg::kMemory), size);
}

MinimigConfig::FastRam MinimigConfig::fast_ram() const noexcept {
    const std::byte memory = at_(mm_cfg::kMemory);
    return kFastCodeField.in<FastRam>(kFastSplitField.code(memory) | kFastHighField.code(memory));
}

void MinimigConfig::set_fast_ram(FastRam size) noexcept {
    const std::byte code{std::to_underlying(size)};
    const std::byte kept = at_(mm_cfg::kMemory)
        & ~(kFastSplitField.placed(kFastSplitField.bits)
            | kFastHighField.placed(kFastHighField.bits));
    at_(mm_cfg::kMemory) = kept | kFastSplitField.placed(code) | kFastHighField.placed(code);
}

void MinimigConfig::set_hrtmon(bool on) noexcept {
    at_(mm_cfg::kMemory) = kHrtmonFlag.into(at_(mm_cfg::kMemory), on);
}

bool MinimigConfig::joy_swap() const noexcept {
    return kJoySwapFlag.in(at_(mm_cfg::kAutofire));
}

void MinimigConfig::set_joy_swap(bool on) noexcept {
    at_(mm_cfg::kAutofire) = kJoySwapFlag.into(at_(mm_cfg::kAutofire), on);
}

MinimigConfig::JoyMode MinimigConfig::joy_mode() const noexcept {
    return kJoyModeField.in<JoyMode>(at_(mm_cfg::kAutofire));
}

void MinimigConfig::set_joy_mode(JoyMode mode) noexcept {
    at_(mm_cfg::kAutofire) = kJoyModeField.into(at_(mm_cfg::kAutofire), mode);
}

bool MinimigConfig::floppy_turbo() const noexcept {
    return at_(mm_cfg::kFloppySpeed) != kZero;
}

void MinimigConfig::set_floppy_turbo(bool on) noexcept {
    at_(mm_cfg::kFloppySpeed) = kFloppyFastFlag.into(at_(mm_cfg::kFloppySpeed), on);
}

void MinimigConfig::set_ide_enabled(bool on) noexcept {
    at_(mm_cfg::kIdeCfg) = kIdeEnableFlag.into(at_(mm_cfg::kIdeCfg), on);
}

bool MinimigConfig::ide_slow() const noexcept {
    return kIdeSlowFlag.in(at_(mm_cfg::kIdeCfg));
}

void MinimigConfig::set_ide_slow(bool on) noexcept {
    at_(mm_cfg::kIdeCfg) = kIdeSlowFlag.into(at_(mm_cfg::kIdeCfg), on);
}

MinimigConfig::UnitMode MinimigConfig::unit_mode(Drive unit) const noexcept {
    const std::byte mode = at_(record_of(unit));
    if (mode > std::byte{std::to_underlying(UnitMode::Removable)})
        return UnitMode::Fixed;
    return kUnitModeField.in<UnitMode>(mode);
}

void MinimigConfig::set_unit_mode(Drive unit, UnitMode mode) noexcept {
    at_(record_of(unit)) = std::byte{std::to_underlying(mode)};
}

MinimigConfig::Scandoubler MinimigConfig::scandoubler() const noexcept {
    return kScandoublerField.in<Scandoubler>(at_(mm_cfg::kScanlines));
}

void MinimigConfig::set_scandoubler(Scandoubler fx) noexcept {
    at_(mm_cfg::kScanlines) = kScandoublerField.into(at_(mm_cfg::kScanlines), fx);
}

bool MinimigConfig::blank_plus() const noexcept {
    return kBlankPlusFlag.in(at_(mm_cfg::kScanlines));
}

void MinimigConfig::set_blank_plus(bool on) noexcept {
    at_(mm_cfg::kScanlines) = kAreaField.into(at_(mm_cfg::kScanlines), on ? kAreaPlus : kZero);
}

MinimigConfig::Aspect MinimigConfig::aspect() const noexcept {
    return kAspectField.in<Aspect>(at_(mm_cfg::kScanlines));
}

void MinimigConfig::set_aspect(Aspect ratio) noexcept {
    at_(mm_cfg::kScanlines) = kAspectField.into(at_(mm_cfg::kScanlines), ratio);
}

MinimigConfig::StereoMix MinimigConfig::stereo_mix() const noexcept {
    return kStereoField.in<StereoMix>(at_(mm_cfg::kAudio));
}

void MinimigConfig::set_stereo_mix(StereoMix mix) noexcept {
    at_(mm_cfg::kAudio) = std::byte{std::to_underlying(mix)};
}

ExtCfgWord MinimigConfig::ext_cfg() const noexcept {
    return ExtCfgWord::of(
        hal::LinkHalves{.low = half_(mm_cfg::kExtCfgLow), .high = half_(mm_cfg::kExtCfgHigh)}
    );
}

void MinimigConfig::set_ext_cfg(ExtCfgWord value) noexcept {
    const hal::LinkHalves halves = value.halves();
    halves.low.store_le(field_(mm_cfg::kExtCfgLow, mm_cfg::kHalf).first<sizeof(hal::LinkWord)>());
    halves.high.store_le(field_(mm_cfg::kExtCfgHigh, mm_cfg::kHalf).first<sizeof(hal::LinkWord)>());
}

std::string_view MinimigConfig::joy_mode_name() const noexcept {
    return kJoyModeNames[std::to_underlying(joy_mode())];
}

hal::LinkWord MinimigConfig::joy_swap_word() const noexcept {
    return hal::LinkWord::load_le(std::array{at_(mm_cfg::kAutofire), kJoySwapBit});
}

hal::LinkWord MinimigConfig::joy_mode_word() const noexcept {
    return hal::LinkWord::load_le(
        std::array{at_(mm_cfg::kAutofire), kJoyModeField.placed(kPairBits)}
    );
}

hal::LinkWord MinimigConfig::cpu_word() const noexcept {
    return low_word(at_(mm_cfg::kCpu) & kCpuWordBits);
}

hal::LinkWord MinimigConfig::chipset_word() const noexcept {
    const std::byte chipset = at_(mm_cfg::kChipset);
    const bool cdtv = at_(record_of(Drive::Cdtv)) != kZero;
    const std::byte machine = cdtv ? chipset | kChipsetCdtvBit : chipset & ~kChipsetCdtvBit;
    return low_word(machine & kChipsetWordBits);
}

hal::LinkWord MinimigConfig::floppy_word() const noexcept {
    const std::byte drives = at_(mm_cfg::kFloppyDrives) & kPairBits;
    const std::byte speed = at_(mm_cfg::kFloppySpeed) & kPairBits;
    return low_word(kFloppyDrivesField.placed(drives) | speed);
}

hal::LinkWord MinimigConfig::memory_word() const noexcept {
    return low_word(at_(mm_cfg::kMemory));
}

hal::LinkWord MinimigConfig::hdd_word(IdeUnits present) const noexcept {
    return low_word((at_(mm_cfg::kIdeCfg) & kIdeWordBits) | present.frame_bits());
}

hal::LinkWord MinimigConfig::video_word() const noexcept {
    const std::byte s = at_(mm_cfg::kScanlines);

    const std::byte high = kVideoAreaField.placed(kAreaField.code(s))
        | kVideoAspectField.placed(kAspectField.code(s));
    return hal::LinkWord::load_le(std::array{s & kScanlineBits, high});
}

hal::LinkWord MinimigConfig::audio_word() const noexcept {
    return low_word(at_(mm_cfg::kAudio));
}

hal::LinkWord MinimigConfig::joy_word() const noexcept {
    return hal::LinkWord::load_le(std::array{at_(mm_cfg::kAutofire), kApplyAutofireMask});
}

hal::LinkHalves MinimigConfig::ext_cfg_halves() const noexcept {
    return hal::LinkHalves{
        .low = half_(mm_cfg::kExtCfgLow).without(kMt32Reset),
        .high = half_(mm_cfg::kExtCfgHigh)
    };
}

bool MinimigConfig::hrtmon() const noexcept {
    return (at_(mm_cfg::kMemory) & kMemoryHrtmon) != kZero;
}

svc::NicLink MinimigConfig::nic_link() const noexcept {
    return svc::nic_link_of(at_(mm_cfg::kNicMode));
}

bool MinimigConfig::ide_enabled() const noexcept {
    return (at_(mm_cfg::kIdeCfg) & kIdeEnable) != kZero;
}

bool MinimigConfig::aga() const noexcept {
    return (at_(mm_cfg::kChipset) & kChipsetAga) != kZero;
}

bool MinimigConfig::ntsc() const noexcept {
    return (at_(mm_cfg::kChipset) & kChipsetNtsc) != kZero;
}

BusAddr MinimigConfig::chip_ram_top() const noexcept {
    const auto steps = std::to_integer<std::uint32_t>(at_(mm_cfg::kMemory) & kPairBits);
    return BusAddr{} + kChipRamStep + kChipRamStep * steps;
}

bool MinimigConfig::drive_named(Drive drive) const noexcept {
    const ByteOffset record = record_of(drive);
    const bool named = at_(record + mm_cfg::kDriveName) != kZero;
    switch (drive) {
        case Drive::Ide0:
        case Drive::Ide1:
        case Drive::Ide2:
        case Drive::Ide3:
            return named;
        case Drive::Cd32:
        case Drive::Cdtv:
            return named && at_(record) != kZero;
        case Drive::kCount:
            break;
    }
    return false;
}

std::string_view MinimigConfig::cpu_name() const noexcept {
    return kCpuNames[std::to_underlying(cpu_model())];
}

std::string_view MinimigConfig::chipset_name() const noexcept {
    return kChipsetNames[std::to_underlying(chipset_model())];
}

std::string_view MinimigConfig::chip_ram_name_() const noexcept {
    return kChipNames[std::to_underlying(chip_ram())];
}

std::string_view MinimigConfig::fast_ram_label() const noexcept {
    const auto& names = cpu_wide() ? kWideFastNames : kPlainFastNames;
    return names[std::to_underlying(fast_ram())];
}

std::string_view MinimigConfig::fast_ram_name_() const noexcept {
    const std::string_view name = fast_ram_label();
    return name == kNoneFitted ? std::string_view{} : name;
}

std::string_view MinimigConfig::slow_ram_name_() const noexcept {
    const std::string_view name = kSlowNames[std::to_underlying(slow_ram())];
    return name == kNoneFitted ? std::string_view{} : name;
}

bool MinimigConfig::any_hard_disk_() const noexcept {
    return std::ranges::any_of(
        std::array{Drive::Ide0, Drive::Ide1, Drive::Ide2, Drive::Ide3},
        [this](Drive d) { return at_(record_of(d)) != kZero; }
    );
}

MinimigConfig::SummaryText MinimigConfig::summary_line() const noexcept {
    const std::string_view fast = fast_ram_name_();
    const std::string_view slow = slow_ram_name_();
    SummaryText out{};
    (void)out.append("CPU: ");
    (void)out.append(cpu_name());
    (void)out.append(", Chipset: ");
    (void)out.append(chipset_name());
    (void)out.append(", ChipRAM: ");
    (void)out.append(chip_ram_name_());
    (void)out.append(", FastRAM: ");
    (void)out.append(fast.empty() ? kNoneFitted : fast);
    (void)out.append(", SlowRAM: ");
    (void)out.append(slow.empty() ? kNoneFitted : slow);
    return out;
}

MinimigConfig::InfoText MinimigConfig::info_line() const noexcept {
    const std::string_view fast = fast_ram_name_();
    const std::string_view slow = slow_ram_name_();
    InfoText out{};
    (void)out.append(infra::affine::from(cpu_name(), kCpuNameSkip));
    (void)out.append("/");
    (void)out.append(chipset_name());
    (void)out.append("/");
    (void)out.append(ntsc() ? "N" : "P");
    if (ide_enabled() && any_hard_disk_())
        (void)out.append("/HD");
    (void)out.append(" ");
    (void)out.append(chip_ram_name_());
    if (!fast.empty()) {
        (void)out.append("+");
        (void)out.append(fast);
    }
    if (!slow.empty()) {
        (void)out.append("+");
        (void)out.append(slow);
    }
    if (hrtmon())
        (void)out.append(" HRT");
    (void)out.append("\n");
    (void)out.append(infra::basename(kickstart()));
    return out;
}

void MinimigConfig::stamp_info() noexcept {

    const InfoText line = info_line();
    const std::span<std::byte> dst = field_(mm_cfg::kInfo, mm_cfg::kInfoBytes);
    *std::ranges::copy(infra::text_bytes(line.view()), dst.begin()).out = kZero;
    dst.back() = kZero;
}

void MinimigConfig::apply(Preset preset, proto::FileSize kickstart) noexcept {
    const auto row = [preset]() noexcept -> std::optional<PresetRow> {
        switch (preset) {
            case Preset::Cd32:
                return kCd32;
            case Preset::Cdtv:
                return kCdtv;
            case Preset::A500:
                return kA500;
            case Preset::A600:
                return kA600;
            case Preset::A1200:
                return kA1200;
            case Preset::kCount:
                break;
        }
        return std::nullopt;
    }();
    if (!row)
        return;
    at_(mm_cfg::kCpu) = row->cpu;
    at_(mm_cfg::kChipset) = row->chipset;
    at_(mm_cfg::kMemory) = row->memory;
    set_kickstart(row->kickstart);
    if (!row->ext_rom.empty() && kickstart < kWholeKickstart)
        set_ext_rom(row->ext_rom);
    at_(mm_cfg::kAutofire) = row->autofire;
    at_(record_of(Drive::Cd32)) = row->drive == Drive::Cd32 ? std::byte{1} : kZero;
    at_(record_of(Drive::Cdtv)) = row->drive == Drive::Cdtv ? std::byte{1} : kZero;
    at_(mm_cfg::kIdeCfg) = kZero;
}

}
