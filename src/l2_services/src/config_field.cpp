// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/config_field.h"

#include <algorithm>
#include <cstring>
#include <span>

#include "infra/endian.h"
#include "svc/option_codec.h"

namespace mister::svc {

namespace {

std::uint32_t load_count(const std::byte* base) {
    return infra::load_native<std::uint32_t>(std::span<const std::byte, 4>(base, 4));
}

void store_count(std::byte* base, std::uint32_t v) {
    infra::store_native<std::uint32_t>(std::span<std::byte, 4>(base, 4), v);
}

}

ConfigField::ConfigField(ConfigSnapshot& snapshot, const Option& row) noexcept
    : base_(std::as_writable_bytes(std::span<ConfigSnapshot, 1>(&snapshot, 1)).data() + row.offset),
      row_(row) {}

void ConfigField::store(std::uint32_t bits) noexcept {
    const OptionCodec& codec = codec_for(row_.type);
    if (codec.store != nullptr)
        codec.store(base_, bits);
}

void ConfigField::assign_string(std::string_view value) noexcept {

    const std::size_t cap = static_cast<std::size_t>(row_.max);
    std::memset(base_, 0, cap);
    std::memcpy(base_, value.data(), std::min(value.size(), cap - 1));
}

void ConfigField::clear_slots() noexcept {
    const std::size_t slots = static_cast<std::size_t>(row_.min);
    const std::size_t item = static_cast<std::size_t>(row_.max);
    for (std::size_t n = 0; n < slots; ++n)
        base_[n * item] = std::byte{0};
}

void ConfigField::fill_first_empty_slot(std::string_view value) noexcept {
    const std::size_t slots = static_cast<std::size_t>(row_.min);
    const std::size_t item = static_cast<std::size_t>(row_.max);
    for (std::size_t n = 0; n < slots; ++n) {
        std::byte* slot = base_ + n * item;
        if (slot[0] != std::byte{0})
            continue;
        std::memset(slot, 0, item);
        std::memcpy(slot, value.data(), std::min(value.size(), item - 1));
        break;
    }
}

void ConfigField::clear_count() noexcept {
    store_count(base_, 0);
}

std::optional<ConfigField> ConfigField::next_element() noexcept {
    const std::size_t capacity = row_.length / sizeof(std::uint32_t);
    const std::uint32_t count = load_count(base_);

    if (static_cast<std::size_t>(count) + 1 >= capacity)
        return std::nullopt;
    const std::uint32_t pos = count + 1;
    store_count(base_, pos);
    return ConfigField{base_ + pos * sizeof(std::uint32_t), row_};
}

}
