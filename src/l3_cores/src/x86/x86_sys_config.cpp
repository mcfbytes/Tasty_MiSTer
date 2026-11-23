// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/x86/x86_sys_config.h"

#include <algorithm>
#include <cstring>
#include <string_view>

#include "cores/x86/x86_sys_config_codec.h"
#include "infra/byte_view.h"
#include "infra/endian.h"
#include "infra/path_parts.h"
#include "infra/span_cut.h"

namespace mister::cores::x86 {

namespace {

constexpr ConfigSlotCount kSlots{1};
constexpr std::string_view kEmptySlot = "< empty slot >";

inline constexpr auto kPathFieldAt = [] {
    infra::IndexedArray<infra::ByteOffset, X86Slot::kCount, X86Slot> at{};
    std::size_t i = 0;
    for (const X86Slot slot : kX86Slots) {
        at[slot] = X86SysConfig::kPathsAt + infra::ByteCount{X86SysConfig::kPathBytes} * i;
        ++i;
    }
    return at;
}();

}

void X86SysConfig::encode(Image img) const noexcept {
    std::ranges::fill(img, std::byte{0});
    infra::store_le<std::uint32_t>(img.first<sizeof(std::uint32_t)>(), kVersion);
    for (const X86Slot slot : kX86Slots) {

        const std::string_view text = path(slot).substr(0, kPathBytes - 1);
        std::memcpy(infra::affine::from(img, kPathFieldAt[slot]).data(), text.data(), text.size());
    }
}

X86SysConfig X86SysConfig::decode(ConstImage img) noexcept {
    X86SysConfig c{};
    if (infra::load_le<std::uint32_t>(img.first<sizeof(std::uint32_t)>()) != kVersion)
        return c;
    for (const X86Slot slot : kX86Slots) {
        c.name(
            slot,
            infra::text_until_nul(infra::affine::from(img, kPathFieldAt[slot]).first<kPathBytes>())
        );
    }
    return c;
}

ConfigSlotCount X86SysConfigCodec::slot_count() const noexcept {
    TASTY_SEAT_BODY(X86SysConfigCodec);
    return kSlots;
}

FixedStr<kConfigNameChars, StrFit::Clip> X86SysConfigCodec::file_name(
    ConfigSlot slot
) const noexcept {
    TASTY_SEAT_BODY(X86SysConfigCodec);
    FixedStr<kConfigNameChars, StrFit::Clip> name{};
    if (slot == kStartupSlot)
        (void)name.assign(file_name_);
    return name;
}

ConfigFit X86SysConfigCodec::fit() const noexcept {
    TASTY_SEAT_BODY(X86SysConfigCodec);
    return ConfigFit::VersionGated;
}

proto::FileSize X86SysConfigCodec::value_bytes() const noexcept {
    TASTY_SEAT_BODY(X86SysConfigCodec);
    return proto::FileSize{sizeof(X86SysConfig)};
}

bool X86SysConfigCodec::decode(ConfigBlob blob, std::span<std::byte> value) const noexcept {
    TASTY_SEAT_BODY(X86SysConfigCodec);
    if (value.size() < sizeof(X86SysConfig))
        return false;
    std::array<std::byte, X86SysConfig::kBytes> img{};
    std::ranges::copy(blob.bytes.first(std::min(blob.bytes.size(), img.size())), img.begin());
    const X86SysConfig v = X86SysConfig::decode(img);
    std::memcpy(value.data(), &v, sizeof v);
    return true;
}

std::span<const std::byte> X86SysConfigCodec::encode(
    std::span<const std::byte> value,
    std::span<std::byte> dst
) const noexcept {
    TASTY_SEAT_BODY(X86SysConfigCodec);
    X86SysConfig v{};
    if (value.size() != infra::bytes_of(v).size() || dst.size() < X86SysConfig::kBytes)
        return {};
    std::memcpy(&v, value.data(), sizeof v);
    const auto img = dst.first<X86SysConfig::kBytes>();
    v.encode(img);
    return img;
}

FixedStr<kConfigPreviewChars, StrFit::Clip> X86SysConfigCodec::preview(
    ConfigBlob blob
) const noexcept {
    TASTY_SEAT_BODY(X86SysConfigCodec);
    FixedStr<kConfigPreviewChars, StrFit::Clip> out{};
    std::array<std::byte, X86SysConfig::kBytes> img{};
    std::ranges::copy(blob.bytes.first(std::min(blob.bytes.size(), img.size())), img.begin());
    const X86SysConfig c = X86SysConfig::decode(img);
    for (const X86Slot slot : kX86Slots) {
        const std::string_view p = c.path(slot);
        if (p.empty())
            continue;
        (void)out.assign(infra::basename(p));
        return out;
    }
    (void)out.assign(kEmptySlot);
    return out;
}

}
