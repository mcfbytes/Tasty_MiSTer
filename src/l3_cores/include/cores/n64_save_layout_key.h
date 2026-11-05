// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "cores/n64_memory_type.h"
#include "proto/types.h"

namespace mister::cores::n64 {

struct SaveLayoutKey {
    MemoryType cart = MemoryType::None;
    bool tpak = false;
    SaveRowCount rows{};

    [[nodiscard]] constexpr proto::ImageTag to_tag() const noexcept;
    friend constexpr bool operator==(const SaveLayoutKey&, const SaveLayoutKey&) = default;
};

struct SaveLayoutTag {
    std::uint8_t v = 0;
    static constexpr std::uint8_t kCartMask = 0x07;
    static constexpr std::uint8_t kTpakBit = 0x08;
    static constexpr unsigned kCountShift = 4;

    [[nodiscard]] static constexpr SaveLayoutTag of(const SaveLayoutKey& key) noexcept {
        const auto raw = static_cast<std::uint32_t>(key.cart);
        const std::size_t c = raw <= static_cast<std::uint32_t>(MemoryType::Flash128k) ? raw : 0u;
        const std::size_t n = key.rows.fits() ? key.rows.size() : 0u;
        return SaveLayoutTag{
            static_cast<std::uint8_t>(c | (key.tpak ? kTpakBit : 0u) | (n << kCountShift))
        };
    }

    [[nodiscard]] static constexpr SaveLayoutTag of(proto::ImageTag tag) noexcept {
        return SaveLayoutTag{tag.v};
    }

    [[nodiscard]] constexpr proto::ImageTag image_tag() const noexcept {
        return proto::ImageTag{v};
    }

    [[nodiscard]] constexpr SaveLayoutKey key() const noexcept {
        return SaveLayoutKey{
            static_cast<MemoryType>(v & kCartMask),
            (v & kTpakBit) != 0,
            SaveRowCount::of(v >> kCountShift)
        };
    }

    friend constexpr bool operator==(SaveLayoutTag, SaveLayoutTag) = default;
};

constexpr proto::ImageTag SaveLayoutKey::to_tag() const noexcept {
    return SaveLayoutTag::of(*this).image_tag();
}

[[nodiscard]] constexpr SaveLayoutKey layout_key_from_tag(proto::ImageTag tag) noexcept {
    return SaveLayoutTag::of(tag).key();
}

}
