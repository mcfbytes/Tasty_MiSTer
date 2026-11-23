// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/minimig_config_codec.h"

#include <cstring>

#include "cores/minimig/minimig_config.h"

namespace mister::cores::minimig {

ConfigSlotCount MinimigConfigCodec::slot_count() const noexcept {
    return ConfigSlotCount{mm_cfg::kSlotCount};
}

FixedStr<kConfigNameChars, StrFit::Clip> MinimigConfigCodec::file_name(
    ConfigSlot slot
) const noexcept {
    FixedStr<kConfigNameChars, StrFit::Clip> name{};
    if (!slot.within(ConfigSlot{mm_cfg::kSlotCount}))
        return name;
    (void)name.assign(mm_cfg::kFileStem);

    if (slot != kStartupSlot) {
        const char digit = slot.digit();
        (void)name.append(std::string_view{&digit, 1});
    }
    (void)name.append(mm_cfg::kFileExt);
    return name;
}

ConfigFit MinimigConfigCodec::fit() const noexcept {
    return ConfigFit::Bounded;
}

proto::FileSize MinimigConfigCodec::value_bytes() const noexcept {
    return proto::FileSize{sizeof(MinimigConfig)};
}

bool MinimigConfigCodec::decode(ConfigBlob blob, std::span<std::byte> value) const noexcept {
    if (value.size() < sizeof(MinimigConfig))
        return false;
    const MinimigConfig v = MinimigConfig::from_file(blob.bytes);
    std::memcpy(value.data(), v.image.data(), v.image.size());
    return true;
}

std::span<const std::byte> MinimigConfigCodec::encode(
    std::span<const std::byte> value,
    std::span<std::byte> dst
) const noexcept {
    MinimigConfig v{};
    if (value.size() != v.image.size() || dst.size() < v.image.size())
        return {};
    std::memcpy(v.image.data(), value.data(), v.image.size());

    v.stamp_info();
    std::memcpy(dst.data(), v.image.data(), v.image.size());
    return dst.first(v.image.size());
}

FixedStr<kConfigPreviewChars, StrFit::Clip> MinimigConfigCodec::preview(
    ConfigBlob blob
) const noexcept {
    return MinimigConfig::preview(blob);
}

}
