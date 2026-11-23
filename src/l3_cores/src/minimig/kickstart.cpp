// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/kickstart.h"

#include <algorithm>
#include <utility>

namespace mister::cores::minimig {

namespace {

std::span<const std::byte> behind(std::span<const std::byte> file, proto::FileSize size) {
    return file.subspan(size.clamp_write(proto::kFileStart, file.size()));
}

std::span<const std::byte> below(std::span<const std::byte> file, proto::FileSize size) {
    return file.first(size.clamp_write(proto::kFileStart, file.size()));
}

std::vector<std::byte> decrypted(std::span<const std::byte> file, Kickstart::Key key) {
    const std::span<const std::byte> body = behind(file, Kickstart::kKeyedHeader);
    std::vector<std::byte> out(body.size());
    for (std::size_t i = 0; i < body.size(); ++i)
        out[i] = body[i] ^ key.bytes[i % key.bytes.size()];
    return out;
}

std::vector<std::byte> copy_of(std::span<const std::byte> bytes) {
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

std::optional<std::vector<std::byte>> rom_slot(std::span<const std::byte> file) {
    const proto::FileSize size = proto::FileSize::of(file);
    if (size == Kickstart::kRom512K)
        return copy_of(file);
    if (size != Kickstart::kRom256K)
        return std::nullopt;
    std::vector<std::byte> out = copy_of(file);
    out.insert(out.end(), file.begin(), file.end());
    return out;
}

Kickstart twice(Kickstart::Form form, std::vector<std::byte> rom, BusAddr second) {
    Kickstart k{.form = form};
    k.regions.push_back({.at = kKickstartRom, .bytes = rom});
    k.regions.push_back({.at = second, .bytes = std::move(rom)});
    return k;
}

}

Kickstart::Key Kickstart::Key::usable(std::span<const std::byte> file) noexcept {
    const proto::FileSize size = proto::FileSize::of(file);
    return size.empty() || size >= kKeyBuffer ? Key{} : Key{.bytes = file};
}

std::optional<Kickstart::Form> Kickstart::form_of(proto::FileSize size, bool keyed) noexcept {
    if (size == kRom1M)
        return Form::Rom1M;
    if (keyed && size == kBootRom + kKeyedHeader)
        return Form::BootRomKeyed;
    if (size == kBootRom)
        return Form::BootRom;
    if (size == kRom512K)
        return Form::Rom512K;
    if (keyed && size == kRom512K + kKeyedHeader)
        return Form::Rom512KKeyed;
    if (size == kRom256K)
        return Form::Rom256K;
    if (keyed && size == kRom256K + kKeyedHeader)
        return Form::Rom256KKeyed;
    return std::nullopt;
}

std::optional<Kickstart> Kickstart::route(std::span<const std::byte> file, Key key) {
    const std::optional<Form> form = form_of(proto::FileSize::of(file), key.present());
    if (!form)
        return std::nullopt;
    switch (*form) {
        case Form::Rom1M: {

            Kickstart k{.form = *form};
            k.regions.push_back({.at = kExtendedRom, .bytes = copy_of(below(file, kRom512K))});
            k.regions.push_back({.at = kKickstartRom, .bytes = copy_of(behind(file, kRom512K))});
            return k;
        }
        case Form::BootRomKeyed:
        case Form::BootRom: {
            Kickstart k{.form = *form, .clears_tag = true};
            k.regions.push_back(
                {.at = kKickstartRom,
                 .bytes = *form == Form::BootRom ? copy_of(file) : decrypted(file, key)}
            );
            return k;
        }
        case Form::Rom512K:
            return twice(*form, copy_of(file), kExtendedRom);
        case Form::Rom512KKeyed:
            return twice(*form, decrypted(file, key), kExtendedRom);
        case Form::Rom256K:
            return twice(*form, copy_of(file), kKickstartMirror);
        case Form::Rom256KKeyed:
            return twice(*form, decrypted(file, key), kKickstartMirror);
        case Form::WithExtRom:
        case Form::kCount:
            break;
    }
    return std::nullopt;
}

std::optional<Kickstart> Kickstart::with_ext_rom(RomPair roms) {

    std::optional<std::vector<std::byte>> high = rom_slot(roms.ext);
    if (!high)
        return std::nullopt;
    std::optional<std::vector<std::byte>> low = rom_slot(roms.kick);
    if (!low)
        return std::nullopt;
    Kickstart k{.form = Form::WithExtRom};
    k.regions.push_back({.at = kExtendedRom, .bytes = std::move(*high)});
    k.regions.push_back({.at = kKickstartRom, .bytes = std::move(*low)});
    return k;
}

}
