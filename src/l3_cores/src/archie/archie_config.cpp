// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/archie/archie_config.h"

#include <algorithm>
#include <cstring>
#include <string_view>

#include "cores/archie/archie_config_codec.h"
#include "infra/byte_view.h"
#include "infra/path_parts.h"
#include "infra/span_cut.h"

namespace mister::cores::archie {

namespace {

constexpr std::string_view kFileName = "ARCHIE.CFG";
constexpr ConfigSlotCount kSlots{1};
constexpr std::string_view kEmptySlot = "< empty slot >";

[[nodiscard]] std::string_view get_path(
    ArchieConfig::ConstImage img,
    infra::ByteOffset at
) noexcept {
    return infra::text_until_nul(infra::affine::from(img, at).first<ArchieConfig::kPathBytes>());
}

void put_path(ArchieConfig::Image img, infra::ByteOffset at, std::string_view text) noexcept {
    std::ranges::copy(std::as_bytes(std::span{text}), infra::affine::from(img, at).begin());
}

}

void ArchieConfig::encode(Image img) const noexcept {
    std::ranges::fill(img, std::byte{0});
    system_ctrl.store_le(infra::affine::from(img, kSystemCtrlAt).first<sizeof(std::uint32_t)>());
    put_path(img, kRomImgAt, rom_img.view());
    constexpr infra::ByteCount kPath{kPathBytes};
    infra::ByteOffset at = kHddImgAt;
    for (const auto& path : hdd_img) {
        put_path(img, at, path.view());
        at += kPath;
    }
}

ArchieConfig ArchieConfig::decode(ConstImage img) noexcept {
    ArchieConfig c{};
    c.system_ctrl = SystemCtrl::load_le(
        infra::affine::from(img, kSystemCtrlAt).first<sizeof(std::uint32_t)>()
    );
    (void)c.rom_img.assign(get_path(img, kRomImgAt));
    constexpr infra::ByteCount kPath{kPathBytes};
    infra::ByteOffset at = kHddImgAt;
    for (auto& path : c.hdd_img) {
        (void)path.assign(get_path(img, at));
        at += kPath;
    }
    return c;
}

ConfigSlotCount ArchieConfigCodec::slot_count() const noexcept {
    return kSlots;
}

FixedStr<kConfigNameChars, StrFit::Clip> ArchieConfigCodec::file_name(
    ConfigSlot slot
) const noexcept {
    FixedStr<kConfigNameChars, StrFit::Clip> name{};
    if (slot == kStartupSlot)
        (void)name.assign(kFileName);
    return name;
}

ConfigFit ArchieConfigCodec::fit() const noexcept {
    return ConfigFit::Strict;
}

proto::FileSize ArchieConfigCodec::value_bytes() const noexcept {
    return proto::FileSize{sizeof(ArchieConfig)};
}

bool ArchieConfigCodec::decode(ConfigBlob blob, std::span<std::byte> value) const noexcept {
    if (value.size() < sizeof(ArchieConfig))
        return false;
    ArchieConfig v{};
    if (blob.bytes.size() == ArchieConfig::kBytes) {
        v = ArchieConfig::decode(ArchieConfig::ConstImage{blob.bytes.data(), ArchieConfig::kBytes});
    }
    std::memcpy(value.data(), &v, sizeof v);
    return true;
}

std::span<const std::byte> ArchieConfigCodec::encode(
    std::span<const std::byte> value,
    std::span<std::byte> dst
) const noexcept {
    ArchieConfig v{};
    if (value.size() != infra::bytes_of(v).size() || dst.size() < ArchieConfig::kBytes)
        return {};
    std::memcpy(&v, value.data(), sizeof v);
    const auto img = dst.first(ArchieConfig::kBytes);
    v.encode(ArchieConfig::Image{img.data(), ArchieConfig::kBytes});
    return img;
}

FixedStr<kConfigPreviewChars, StrFit::Clip> ArchieConfigCodec::preview(
    ConfigBlob blob
) const noexcept {
    FixedStr<kConfigPreviewChars, StrFit::Clip> out{};
    if (blob.bytes.size() != ArchieConfig::kBytes) {
        (void)out.assign(kEmptySlot);
        return out;
    }
    const ArchieConfig c = ArchieConfig::decode(
        ArchieConfig::ConstImage{blob.bytes.data(), ArchieConfig::kBytes}
    );
    (void)out.assign(infra::basename(c.rom_img.view()));
    return out;
}

}
