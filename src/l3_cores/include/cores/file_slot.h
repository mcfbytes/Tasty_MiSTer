// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/disk_mirror.h"
#include "cores/mount_index_form.h"
#include "cores/types.h"
#include "infra/seat.h"
#include "svc/disk_format_row.h"

namespace mister::cores {

class IRomsetAliasSource;

enum class SlotRole : std::uint8_t { Image, Disc, Save, Ladder, Drive };

[[nodiscard]] constexpr bool slot_service_serves(SlotRole role) {
    return role == SlotRole::Save || role == SlotRole::Drive;
}

enum class StartMount : std::uint8_t { Generic, AsPick, Unmodelled };

struct FileSlot {
    TASTY_SEAT_EXEMPT(const_shared);
    IoIndex index;
    std::string_view extensions;
    std::string_view label;
    bool writable = false;
    bool required = false;
    SlotRole role = SlotRole::Image;

    std::optional<std::string_view> romset_browse{};
    bool no_enter = false;

    std::unique_ptr<IRomsetAliasSource> (*romset_aliases)() = nullptr;

    bool opens_zip = false;

    StartMount start_mount = StartMount::Generic;

    std::span<const svc::DiskFormatRow> disk_formats{};

    bool via_slot_service = false;

    MountIndexForm mount_index_form = MountIndexForm::ExtOrdinal;

    std::optional<DiskMirror> mirror{};
};

[[nodiscard]] constexpr bool walks_romset(std::span<const FileSlot> slots, IoIndex index) {
    for (const FileSlot& s : slots) {
        if (s.index == index && s.romset_browse.has_value())
            return true;
    }
    return false;
}

[[nodiscard]] constexpr bool is_load_row(const FileSlot& s) {
    return s.romset_browse.has_value();
}

[[nodiscard]] constexpr std::optional<DiskMirror> mirror_of(
    std::span<const FileSlot> slots,
    IoIndex index
) {
    for (const FileSlot& s : slots)
        if (s.index == index && !is_load_row(s))
            return s.mirror;
    return std::nullopt;
}

[[nodiscard]] constexpr SlotRole role_of(
    std::span<const FileSlot> slots,
    IoIndex index,
    SlotRole undeclared = SlotRole::Image
) {
    for (const FileSlot& s : slots)
        if (s.index == index && !is_load_row(s))
            return s.role;
    return undeclared;
}

[[nodiscard]] constexpr MountIndexForm mount_index_form_of(
    std::span<const FileSlot> slots,
    IoIndex index
) {
    for (const FileSlot& s : slots)
        if (s.index == index && !is_load_row(s))
            return s.mount_index_form;
    return MountIndexForm::ExtOrdinal;
}

[[nodiscard]] constexpr StartMount start_mount_of(
    std::span<const FileSlot> slots,
    IoIndex index,
    StartMount undeclared = StartMount::Generic
) {
    for (const FileSlot& s : slots)
        if (s.index == index && !is_load_row(s))
            return s.start_mount;
    return undeclared;
}

[[nodiscard]] constexpr std::span<const svc::DiskFormatRow> disk_formats_of(
    std::span<const FileSlot> slots,
    IoIndex index,
    std::span<const svc::DiskFormatRow> fallback
) {
    for (const FileSlot& s : slots)
        if (s.index == index && !is_load_row(s) && !s.disk_formats.empty())
            return s.disk_formats;
    return fallback;
}

[[nodiscard]] constexpr bool mounts_via_slot_service(
    std::span<const FileSlot> slots,
    IoIndex index
) {
    for (const FileSlot& s : slots)
        if (s.index == index && !is_load_row(s))
            return s.via_slot_service;
    return false;
}

[[nodiscard]] constexpr std::size_t disc_rows(std::span<const FileSlot> slots) {
    std::size_t n = 0;
    for (const FileSlot& s : slots)
        n += s.role == SlotRole::Disc ? 1u : 0u;
    return n;
}

[[nodiscard]] constexpr std::size_t ladder_rows(std::span<const FileSlot> slots) {
    std::size_t n = 0;
    for (const FileSlot& s : slots)
        n += s.role == SlotRole::Ladder ? 1u : 0u;
    return n;
}

void a_staging_profile_declares_exactly_one_disc_slot();

consteval IoIndex disc_slot_of(std::span<const FileSlot> slots) {
    if (disc_rows(slots) != 1)
        a_staging_profile_declares_exactly_one_disc_slot();
    for (const FileSlot& s : slots)
        if (s.role == SlotRole::Disc)
            return s.index;
    return IoIndex{};
}

}
