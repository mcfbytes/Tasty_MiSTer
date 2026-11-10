// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/st_option_row.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <utility>

#include "cores/st_config.h"
#include "infra/indexed_array.h"

namespace mister::cores {

namespace {

constexpr std::string_view kWriteProtectGlyph = "\x17";
constexpr std::string_view kNoDisk = "* no disk *";
constexpr std::string_view kNoCartridge = "* no cartridge *";

template <std::size_t... I>
constexpr std::array<StOptionRowPlain, sizeof...(I)> plain_rows(
    std::index_sequence<I...>
) noexcept {
    return {StOptionRowPlain{static_cast<StOption>(I)}...};
}

constexpr proto::FieldValue kOnSlot{1};
constexpr proto::FieldValue kAspectFullScreen{1};
constexpr proto::FieldValue kAspectArc1{2};
constexpr proto::FieldValue kAspectArc2{3};
constexpr proto::FieldValue kCropFullSlot{0};
constexpr proto::FieldValue kCrop216Slot{2};

constexpr std::size_t kIds = 256;
constinit const std::array<StOptionRowPlain, kIds> kPlain = plain_rows(
    std::make_index_sequence<kIds>{}
);
constinit const StOptionRowBlitter kBlitter{};
constinit const StOptionRowViking kViking{};
constinit const StOptionRowAspect kAspect{};
constinit const StOptionRowVideoCrop kVideoCrop{};
constinit const StOptionRowFloppy kFloppyA{StOption::FloppyA, st_ctrl::StSlot::FloppyA};
constinit const StOptionRowFloppy kFloppyB{StOption::FloppyB, st_ctrl::StSlot::FloppyB};
constinit const StOptionRowHdd kHdd0{StOption::Hdd0, AcsiTarget{0}};
constinit const StOptionRowHdd kHdd1{StOption::Hdd1, AcsiTarget{1}};
constinit const StOptionRowTos kTos{};
constinit const StOptionRowCartridge kCartridge{};

}

OptionRowView StOptionRowBlitter::on_cycle_view(
    const Facts& f,
    const OptionRow::CycleArm& r
) const noexcept {
    const st_ctrl::CtrlWord sc = f.system_ctrl;
    const bool ste = sc.chipset() != st_ctrl::CtrlWord::kChipsetSt;
    const bool bit = sc.has(st_ctrl::CtrlWord::kBlitter);
    return {static_cast<std::uint8_t>(bit ? 1 : 0), ste, r.values[(bit || ste) ? 1u : 0u]};
}

Ex<StOptionRow::Write> StOptionRowBlitter::on_cycle_set(
    const Facts& f,
    proto::FieldValue choice
) const {
    const st_ctrl::CtrlWord sc = f.system_ctrl;

    if (sc.has(st_ctrl::CtrlWord::kSte))
        return Write{};
    return Write{
        .system_ctrl = choice == kOnSlot ? sc.with(st_ctrl::CtrlWord::kBlitter)
                                         : sc.without(st_ctrl::CtrlWord::kBlitter)
    };
}

OptionRowView StOptionRowViking::on_cycle_view(
    const Facts& f,
    const OptionRow::CycleArm& r
) const noexcept {
    const st_ctrl::CtrlWord sc = f.system_ctrl;
    const bool room = sc.mem_config() <= st_ctrl::CtrlWord::kMem8M;
    const bool bit = sc.has(st_ctrl::CtrlWord::kViking);
    return {static_cast<std::uint8_t>(bit ? 1 : 0), !room, r.values[(bit && room) ? 1u : 0u]};
}

Ex<StOptionRow::Write> StOptionRowViking::on_cycle_set(
    const Facts& f,
    proto::FieldValue choice
) const {
    const st_ctrl::CtrlWord sc = f.system_ctrl;
    return Write{
        .system_ctrl = choice == kOnSlot ? sc.with(st_ctrl::CtrlWord::kViking)
                                         : sc.without(st_ctrl::CtrlWord::kViking)
    };
}

OptionRowView StOptionRowAspect::on_cycle_view(
    const Facts& f,
    const OptionRow::CycleArm& r
) const noexcept {
    const auto ar = f.system_ctrl.aspect();
    return {ar.v, false, infra::at_index(r.values, ar)};
}

Ex<StOptionRow::Write> StOptionRowAspect::on_cycle_set(
    const Facts& f,
    proto::FieldValue choice
) const {
    st_ctrl::CtrlWord v = f.system_ctrl.without(st_ctrl::CtrlWord::kVideoAspect1)
                              .without(st_ctrl::CtrlWord::kVideoAspect2);

    const bool first = choice == kAspectFullScreen || choice == kAspectArc2;
    const bool second = choice == kAspectArc1 || choice == kAspectArc2;
    if (first)
        v = v.with(st_ctrl::CtrlWord::kVideoAspect1);
    if (second)
        v = v.with(st_ctrl::CtrlWord::kVideoAspect2);
    return Write{.system_ctrl = v};
}

OptionRowView StOptionRowVideoCrop::on_cycle_view(
    const Facts& f,
    const OptionRow::CycleArm& r
) const noexcept {
    const st_ctrl::CtrlWord sc = f.system_ctrl;

    const bool border = sc.has(st_ctrl::CtrlWord::kBorder);
    const bool p216 = (f.ext_ctrl & st_ctrl::kExtCrop216) != 0;
    const std::uint8_t choice = !border ? 0 : p216 ? 2 : 1;
    return {choice, false, r.values[choice]};
}

Ex<StOptionRow::Write> StOptionRowVideoCrop::on_cycle_set(
    const Facts& f,
    proto::FieldValue choice
) const {
    const st_ctrl::CtrlWord sc = f.system_ctrl;

    return Write{
        .system_ctrl = choice == kCropFullSlot ? sc.without(st_ctrl::CtrlWord::kBorder)
                                               : sc.with(st_ctrl::CtrlWord::kBorder),
        .ext_ctrl = choice == kCrop216Slot ? f.ext_ctrl | st_ctrl::kExtCrop216
                                           : f.ext_ctrl & ~st_ctrl::kExtCrop216
    };
}

OptionRowView StOptionRowFloppy::on_file_view(const Facts& f, std::span<char> text) const noexcept {
    const bool wp = f.system_ctrl.has(st_ctrl::write_protect_bit(drive_));
    const std::string_view path = infra::at_index(f.floppy, drive_);
    const bool inserted = !path.empty();
    const std::string_view name = inserted ? StConfig::leaf_of(path) : kNoDisk;
    const int n = std::snprintf(
        text.data(),
        text.size(),
        "%s%.*s",
        wp ? kWriteProtectGlyph.data() : "",
        static_cast<int>(name.size()),
        name.data()
    );
    const auto len = std::min(static_cast<std::size_t>(n > 0 ? n : 0), text.size() - 1);
    return {static_cast<std::uint8_t>(inserted ? 1 : 0), false, std::string_view{text.data(), len}};
}

OptionRowView StOptionRowHdd::on_file_view(const Facts& f, std::span<char>) const noexcept {
    const StOptionRow::AcsiFact& disk = f.acsi[target_];
    return {
        static_cast<std::uint8_t>(disk.bound ? 1 : 0),
        false,
        disk.bound ? StConfig::leaf_of(disk.path) : kNoDisk
    };
}

OptionRowView StOptionRowTos::on_file_view(const Facts& f, std::span<char>) const noexcept {
    return {static_cast<std::uint8_t>(f.tos.empty() ? 0 : 1), false, StConfig::leaf_of(f.tos)};
}

OptionRowView StOptionRowCartridge::on_file_view(const Facts& f, std::span<char>) const noexcept {
    return {
        static_cast<std::uint8_t>(f.cart.empty() ? 0 : 1),
        f.system_ctrl.has(st_ctrl::CtrlWord::kDongle),
        f.cart.empty() ? kNoCartridge : StConfig::leaf_of(f.cart)
    };
}

const StOptionRow& StOptionRow::of(StOption o) noexcept {
    switch (o) {
        case StOption::Blitter:
            return kBlitter;
        case StOption::Viking:
            return kViking;
        case StOption::Aspect:
            return kAspect;
        case StOption::VideoCrop:
            return kVideoCrop;
        case StOption::FloppyA:
            return kFloppyA;
        case StOption::FloppyB:
            return kFloppyB;
        case StOption::Hdd0:
            return kHdd0;
        case StOption::Hdd1:
            return kHdd1;
        case StOption::Tos:
            return kTos;
        case StOption::Cartridge:
            return kCartridge;
        case StOption::None:
        case StOption::WriteProtect:
        case StOption::JoySwap:
        case StOption::ModifyConfig:
        case StOption::LoadConfig:
        case StOption::SaveConfig:
        case StOption::Mt32Pi:
        case StOption::Reset:
        case StOption::ColdBoot:
        case StOption::Memory:
        case StOption::Chipset:
        case StOption::Screen:
        case StOption::Mono60:
        case StOption::Scale:
        case StOption::Scanlines:
        case StOption::Dongle:
        case StOption::YmAudio:
        case StOption::kCount:
            return kPlain[static_cast<std::uint8_t>(o)];
    }

    return kPlain[static_cast<std::uint8_t>(o)];
}

OptionRowView StOptionRow::cycle_view(const Facts& f, const OptionRow::CycleArm& r) const noexcept {
    TASTY_SEAT_BODY(StOptionRow);
    return on_cycle_view(f, r);
}

OptionRowView StOptionRow::file_view(const Facts& f, std::span<char> text) const noexcept {
    TASTY_SEAT_BODY(StOptionRow);
    return on_file_view(f, text);
}

Ex<StOptionRow::Write> StOptionRow::cycle_set(const Facts& f, proto::FieldValue choice) const {
    TASTY_SEAT_BODY(StOptionRow);
    return on_cycle_set(f, choice);
}

OptionRowView StOptionRow::on_cycle_view(const Facts&, const OptionRow::CycleArm&) const noexcept {
    return {};
}

OptionRowView StOptionRow::on_file_view(const Facts&, std::span<char>) const noexcept {
    return {};
}

Ex<StOptionRow::Write> StOptionRow::on_cycle_set(const Facts&, proto::FieldValue) const {
    return std::unexpected(Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(id_)});
}

}
