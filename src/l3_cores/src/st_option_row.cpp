// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/st_option_row.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <utility>

#include "cores/st_config.h"
#include "cores/st_sysctrl.h"

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

constexpr std::size_t kIds = 256;
constinit const std::array<StOptionRowPlain, kIds> kPlain = plain_rows(
    std::make_index_sequence<kIds>{}
);
constinit const StOptionRowBlitter kBlitter{};
constinit const StOptionRowViking kViking{};
constinit const StOptionRowAspect kAspect{};
constinit const StOptionRowVideoCrop kVideoCrop{};
constinit const StOptionRowFloppy kFloppyA{StOption::FloppyA, 0};
constinit const StOptionRowFloppy kFloppyB{StOption::FloppyB, 1};
constinit const StOptionRowHdd kHdd0{StOption::Hdd0, 0};
constinit const StOptionRowHdd kHdd1{StOption::Hdd1, 1};
constinit const StOptionRowTos kTos{};
constinit const StOptionRowCartridge kCartridge{};

}

OptionRowView StOptionRowBlitter::on_cycle_view(const Facts& f, const OptionRow& r) const noexcept {
    const std::uint32_t sc = f.system_ctrl;
    const bool ste = (sc & st_ctrl::kChipsetMask) != 0;
    const bool bit = (sc & st_ctrl::kBlitter) != 0;
    return {static_cast<std::uint8_t>(bit ? 1 : 0), ste, r.values[(bit || ste) ? 1 : 0]};
}

Ex<StOptionRow::Write> StOptionRowBlitter::on_cycle_set(const Facts& f, std::uint8_t choice) const {
    const std::uint32_t sc = f.system_ctrl;

    if ((sc & st_ctrl::kSte) != 0)
        return Write{};
    return Write{
        .system_ctrl = (choice & 1u) != 0 ? sc | st_ctrl::kBlitter : sc & ~st_ctrl::kBlitter
    };
}

OptionRowView StOptionRowViking::on_cycle_view(const Facts& f, const OptionRow& r) const noexcept {
    const std::uint32_t sc = f.system_ctrl;
    const bool room = (sc & st_ctrl::kMemConfigMask) <= st_ctrl::kMemConfig8M;
    const bool bit = (sc & st_ctrl::kViking) != 0;
    return {static_cast<std::uint8_t>(bit ? 1 : 0), !room, r.values[(bit && room) ? 1 : 0]};
}

Ex<StOptionRow::Write> StOptionRowViking::on_cycle_set(const Facts& f, std::uint8_t choice) const {
    const std::uint32_t sc = f.system_ctrl;
    return Write{
        .system_ctrl = (choice & 1u) != 0 ? sc | st_ctrl::kViking : sc & ~st_ctrl::kViking
    };
}

OptionRowView StOptionRowAspect::on_cycle_view(const Facts& f, const OptionRow& r) const noexcept {
    const std::uint32_t sc = f.system_ctrl;
    const auto ar = static_cast<std::uint8_t>(
        ((sc & st_ctrl::kVideoAspect1) != 0 ? 1u : 0u)
        | ((sc & st_ctrl::kVideoAspect2) != 0 ? 2u : 0u)
    );
    return {ar, false, r.values[ar]};
}

Ex<StOptionRow::Write> StOptionRowAspect::on_cycle_set(const Facts& f, std::uint8_t choice) const {
    const std::uint32_t sc = f.system_ctrl;
    std::uint32_t v = sc & ~(st_ctrl::kVideoAspect1 | st_ctrl::kVideoAspect2);
    if ((choice & 1u) != 0)
        v |= st_ctrl::kVideoAspect1;
    if ((choice & 2u) != 0)
        v |= st_ctrl::kVideoAspect2;
    return Write{.system_ctrl = v};
}

OptionRowView StOptionRowVideoCrop::on_cycle_view(
    const Facts& f,
    const OptionRow& r
) const noexcept {
    const std::uint32_t sc = f.system_ctrl;

    const bool border = (sc & st_ctrl::kBorder) != 0;
    const bool p216 = (f.ext_ctrl & st_ctrl::kExtCrop216) != 0;
    const std::uint8_t choice = !border ? 0 : p216 ? 2 : 1;
    return {choice, false, r.values[choice]};
}

Ex<StOptionRow::Write> StOptionRowVideoCrop::on_cycle_set(
    const Facts& f,
    std::uint8_t choice
) const {
    const std::uint32_t sc = f.system_ctrl;

    return Write{
        .system_ctrl = choice != 0 ? sc | st_ctrl::kBorder : sc & ~st_ctrl::kBorder,
        .ext_ctrl = choice == 2 ? f.ext_ctrl | st_ctrl::kExtCrop216
                                : f.ext_ctrl & ~st_ctrl::kExtCrop216
    };
}

OptionRowView StOptionRowFloppy::on_file_view(const Facts& f, std::span<char> text) const noexcept {
    const std::uint8_t i = drive_;
    const bool wp = (f.system_ctrl & st_ctrl::write_protect_bit(i)) != 0;
    const std::string_view name = f.floppy_in[i] ? StConfig::leaf_of(f.floppy[i]) : kNoDisk;
    const int n = std::snprintf(
        text.data(),
        text.size(),
        "%s%.*s",
        wp ? kWriteProtectGlyph.data() : "",
        static_cast<int>(name.size()),
        name.data()
    );
    const auto len = std::min(static_cast<std::size_t>(n > 0 ? n : 0), text.size() - 1);
    return {
        static_cast<std::uint8_t>(f.floppy_in[i] ? 1 : 0),
        false,
        std::string_view{text.data(), len}
    };
}

OptionRowView StOptionRowHdd::on_file_view(const Facts& f, std::span<char>) const noexcept {
    const std::uint8_t t = target_;
    return {
        static_cast<std::uint8_t>(f.hdd_bound[t] ? 1 : 0),
        false,
        f.hdd_bound[t] ? StConfig::leaf_of(f.acsi[t]) : kNoDisk
    };
}

OptionRowView StOptionRowTos::on_file_view(const Facts& f, std::span<char>) const noexcept {
    return {static_cast<std::uint8_t>(f.tos.empty() ? 0 : 1), false, StConfig::leaf_of(f.tos)};
}

OptionRowView StOptionRowCartridge::on_file_view(const Facts& f, std::span<char>) const noexcept {
    return {
        static_cast<std::uint8_t>(f.cart.empty() ? 0 : 1),
        (f.system_ctrl & st_ctrl::kDongle) != 0,
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

OptionRowView StOptionRow::cycle_view(const Facts& f, const OptionRow& r) const noexcept {
    TASTY_SEAT_BODY(StOptionRow);
    return on_cycle_view(f, r);
}

OptionRowView StOptionRow::file_view(const Facts& f, std::span<char> text) const noexcept {
    TASTY_SEAT_BODY(StOptionRow);
    return on_file_view(f, text);
}

Ex<StOptionRow::Write> StOptionRow::cycle_set(const Facts& f, std::uint8_t choice) const {
    TASTY_SEAT_BODY(StOptionRow);
    return on_cycle_set(f, choice);
}

OptionRowView StOptionRow::on_cycle_view(const Facts&, const OptionRow&) const noexcept {
    return {};
}

OptionRowView StOptionRow::on_file_view(const Facts&, std::span<char>) const noexcept {
    return {};
}

Ex<StOptionRow::Write> StOptionRow::on_cycle_set(const Facts&, std::uint8_t) const {
    return std::unexpected(Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(id_)});
}

}
