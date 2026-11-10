// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/st_config.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "cores/config_slot_count.h"
#include "cores/st_sysctrl.h"
#include "infra/byte_view.h"
#include "infra/endian.h"
#include "infra/indexed_array.h"

namespace mister::cores {

namespace {

std::uint32_t get_u32(std::span<const std::byte> img, std::size_t off) noexcept {
    return infra::load_le<std::uint32_t>(img.subspan(off).first<4>());
}

void put_u32(std::span<std::byte> img, std::size_t off, std::uint32_t v) noexcept {
    infra::store_le<std::uint32_t>(img.subspan(off).first<4>(), v);
}

std::string_view get_str(std::span<const std::byte> img, std::size_t off) noexcept {
    return infra::text_until_nul(img.subspan(off).first<st_cfg::kPathBytes>());
}

void put_str(std::span<std::byte> img, std::size_t off, std::string_view s) noexcept {
    std::memcpy(img.data() + off, s.data(), s.size());
}

constexpr std::array<std::string_view, st_ctrl::CtrlWord::kMemConfigCount> kMemNames =
    {"512kb", "1mb", "2mb", "4mb", "8mb", "14mb", "--", "--"};
constexpr std::array<std::string_view, st_ctrl::CtrlWord::kChoiceCount> kChipsetShort =
    {"ST", "STe", "MST", "ST+"};
constexpr std::array<std::string_view, st_ctrl::CtrlWord::kChoiceCount> kAspectNames =
    {"A0 ", "A1 ", "A2 ", "A3 "};
constexpr std::array<std::string_view, st_ctrl::CtrlWord::kChoiceCount> kScanNames =
    {"", "S1 ", "S2 ", "S3 "};
constexpr std::string_view kEmptySlot = "< empty slot >";

}

std::string_view StConfig::leaf_of(std::string_view path) noexcept {
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

StConfig StConfig::defaults() noexcept {
    StConfig c{};
    c.system_ctrl = st_ctrl::CtrlWord{st_ctrl::kDefaultSystemCtrl};
    (void)c.tos_img.assign("games/AtariST/TOS.IMG");
    return c;
}

void StConfig::overlay(
    std::span<const std::byte> blob,
    std::span<std::byte, st_cfg::kBytes> img
) noexcept {
    encode(img);
    const std::size_t n = std::min(blob.size(), img.size());
    if (n != 0)
        std::memcpy(img.data(), blob.data(), n);
    decode(img);
}

void StConfig::encode(std::span<std::byte, st_cfg::kBytes> img) const noexcept {
    std::ranges::fill(img, std::byte{0});
    system_ctrl.store_le(img.subspan(st_cfg::kSystemCtrl).first<4>());
    put_str(img, st_cfg::kTosImg, tos_img.view());
    put_str(img, st_cfg::kCartImg, cart_img.view());
    put_str(img, st_cfg::kAcsiImg0, acsi_img[AcsiTarget{0}].view());
    put_str(img, st_cfg::kAcsiImg1, acsi_img[AcsiTarget{1}].view());
    put_u32(img, st_cfg::kExtCtrl, ext_ctrl);
}

void StConfig::decode(std::span<const std::byte, st_cfg::kBytes> img) noexcept {
    system_ctrl = st_ctrl::CtrlWord{get_u32(img, st_cfg::kSystemCtrl)};
    (void)tos_img.assign(get_str(img, st_cfg::kTosImg));
    (void)cart_img.assign(get_str(img, st_cfg::kCartImg));
    (void)acsi_img[AcsiTarget{0}].assign(get_str(img, st_cfg::kAcsiImg0));
    (void)acsi_img[AcsiTarget{1}].assign(get_str(img, st_cfg::kAcsiImg1));
    ext_ctrl = get_u32(img, st_cfg::kExtCtrl);
}

FixedStr<kConfigPreviewChars, StrFit::Clip> StConfig::preview(ConfigBlob blob) noexcept {
    FixedStr<kConfigPreviewChars, StrFit::Clip> out{};
    if (blob.bytes.empty()) {
        (void)out.assign(kEmptySlot);
        return out;
    }

    std::array<std::byte, st_cfg::kBytes> tmp{};
    std::memcpy(tmp.data(), blob.bytes.data(), std::min(blob.bytes.size(), tmp.size()));
    const st_ctrl::CtrlWord sc{get_u32(tmp, st_cfg::kSystemCtrl)};
    (void)out.append(infra::at_index(kMemNames, sc.mem_config()));
    (void)out.append(" ");
    if (!get_str(tmp, st_cfg::kAcsiImg0).empty())
        (void)out.append("H0 ");
    if (!get_str(tmp, st_cfg::kAcsiImg1).empty())
        (void)out.append("H1 ");
    const auto chipset = sc.chipset();
    (void)out.append(infra::at_index(kChipsetShort, chipset));
    (void)out.append(" ");
    if (chipset == st_ctrl::CtrlWord::kChipsetSt && sc.has(st_ctrl::CtrlWord::kBlitter)) {
        (void)out.append("B ");
    }
    if (sc.has(st_ctrl::CtrlWord::kViking))
        (void)out.append("V ");
    (void)out.append(infra::at_index(kAspectNames, sc.aspect()));
    if (sc.has(st_ctrl::CtrlWord::kVideoColor)) {
        (void)out.append("C ");
    } else if (sc.has(st_ctrl::CtrlWord::kMde60)) {
        (void)out.append("M6 ");
    } else {
        (void)out.append("M ");
    }
    if (!sc.has(st_ctrl::CtrlWord::kBorder))
        (void)out.append("F ");
    (void)out.append(infra::at_index(kScanNames, sc.scanlines()));
    (void)out.append(sc.has(st_ctrl::CtrlWord::kStereo) ? "AS " : "AM ");

    if (const auto tos = get_str(tmp, st_cfg::kTosImg); !tos.empty()) {
        const std::string_view leaf = leaf_of(tos);
        if (leaf.size() >= st_cfg::kTosExtChars) {
            (void)out.append(leaf.substr(0, leaf.size() - st_cfg::kTosExtChars));
        } else {
            (void)out.append(leaf);
        }
    }
    return out;
}

ConfigSlotCount StConfigCodec::slot_count() const noexcept {
    return ConfigSlotCount{st_cfg::kSlotCount};
}

FixedStr<kConfigNameChars, StrFit::Clip> StConfigCodec::file_name(ConfigSlot slot) const noexcept {
    FixedStr<kConfigNameChars, StrFit::Clip> name{};
    if (!slot.within(ConfigSlot{st_cfg::kSlotCount}))
        return name;
    char buf[kConfigNameChars]{};
    (void)std::snprintf(
        buf,
        sizeof(buf),
        "%s%c%s",
        st_cfg::kFileStem,
        slot.digit(),
        st_cfg::kFileExt
    );
    (void)name.assign(buf);
    return name;
}

ConfigFit StConfigCodec::fit() const noexcept {
    return ConfigFit::Partial;
}

proto::FileSize StConfigCodec::value_bytes() const noexcept {
    return proto::FileSize{sizeof(StConfig)};
}

bool StConfigCodec::decode(ConfigBlob blob, std::span<std::byte> value) const noexcept {
    if (value.size() < sizeof(StConfig))
        return false;

    StConfig v = StConfig::defaults();
    std::array<std::byte, st_cfg::kBytes> img{};
    v.overlay(blob.bytes, img);
    std::memcpy(value.data(), &v, sizeof v);
    return true;
}

std::span<const std::byte> StConfigCodec::encode(
    std::span<const std::byte> value,
    std::span<std::byte> dst
) const noexcept {
    StConfig v{};
    if (value.size() != infra::bytes_of(v).size() || dst.size() < st_cfg::kBytes)
        return {};
    std::memcpy(&v, value.data(), sizeof v);
    const auto img = dst.first(st_cfg::kBytes);
    v.encode(std::span<std::byte, st_cfg::kBytes>{img.data(), st_cfg::kBytes});
    return img;
}

FixedStr<kConfigPreviewChars, StrFit::Clip> StConfigCodec::preview(ConfigBlob blob) const noexcept {
    return StConfig::preview(blob);
}

}
