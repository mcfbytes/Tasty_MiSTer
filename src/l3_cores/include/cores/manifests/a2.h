// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "cores/core_profile.h"
#include "svc/disk_codec.h"
#include "svc/disk_format_row.h"

namespace mister::cores::manifests {

inline constexpr auto kNibbleDriveFormats = std::to_array<svc::DiskFormatRow>({
    {.ext = "dsk",
     .codec = svc::DiskCodec::Nib,
     .why = "user_io.cpp:2154-2181: a .dsk on a nibble drive is SD_TYPE_A2, served "
            "by a2_readDSK / a2_writeDSK"},
    {.ext = "do",
     .codec = svc::DiskCodec::Nib,
     .why = "a .do has the .dsk byte layout; stock translates it on the Pravetz "
            "drives only (user_io.cpp:2161-2162, :2177)"},
    {.ext = {},
     .codec = svc::DiskCodec::Raw,
     .why = "user_io.cpp:2148-2150: a .nib, a .po and any other image is the file"},
});

inline constexpr std::array kExtendedCpcDisk{
    std::byte{'E'},
    std::byte{'X'},
    std::byte{'T'},
    std::byte{'E'},
    std::byte{'N'},
    std::byte{'D'},
    std::byte{'E'},
    std::byte{'D'},
    std::byte{' '},
    std::byte{'C'},
    std::byte{'P'},
    std::byte{'C'},
    std::byte{' '},
    std::byte{'D'},
    std::byte{'S'},
    std::byte{'K'},
};

inline constexpr auto kSharedDriveFormats = std::to_array<svc::DiskFormatRow>({
    kNibbleDriveFormats[0],
    kNibbleDriveFormats[1],
    {.ext = "dsk",
     .magic = kExtendedCpcDisk,
     .codec = svc::DiskCodec::Raw,
     .why = "an extended CPC disk is the Microdisc's own image; the magic pass runs first "
            "and reads no extension"},
    kNibbleDriveFormats[2],
});

inline constexpr auto kTk2000Slots = std::to_array<FileSlot>({
    {.index = IoIndex{0},
     .extensions = "NIBDSKDO PO ",
     .label = "",
     .writable = true,
     .role = SlotRole::Drive,
     .disk_formats = kNibbleDriveFormats},
    {.index = IoIndex{1},
     .extensions = "NIBDSKDO PO ",
     .label = "",
     .writable = true,
     .role = SlotRole::Drive,
     .disk_formats = kNibbleDriveFormats},
});

inline constexpr auto kOricSlots = std::to_array<FileSlot>({
    {.index = IoIndex{0},
     .extensions = "NIBDSKDO ",
     .label = "Mount Drive A:",
     .writable = true,
     .role = SlotRole::Drive,
     .disk_formats = kSharedDriveFormats},
    {.index = IoIndex{1},
     .extensions = "NIBDSKDO ",
     .label = "Mount Drive B:",
     .writable = true,
     .role = SlotRole::Drive,
     .disk_formats = kSharedDriveFormats},
    {.index = IoIndex{2},
     .extensions = "DSK",
     .label = "Mount Drive C:",
     .writable = true,
     .role = SlotRole::Drive},
    {.index = IoIndex{3},
     .extensions = "DSK",
     .label = "Mount Drive D:",
     .writable = true,
     .role = SlotRole::Drive},
});

consteval CoreProfile nibble_drive_profile(
    CoreKind kind,
    std::string_view name,
    std::span<const FileSlot> slots
) {
    return CoreProfile{
        .kind = kind,
        .name = name,
        .rbf = "",
        .services = {},
        .slots = slots,
        .boot_assets = {},
        .signatures = {},
        .undeclared_start_mount = StartMount::Unmodelled,
    };
}

inline constexpr CoreProfile kTk2000 = nibble_drive_profile(
    CoreKind::Tk2000,
    "TK2000",
    kTk2000Slots
);
inline constexpr CoreProfile kOric = nibble_drive_profile(CoreKind::Oric, "Oric", kOricSlots);

static_assert(
    kNibbleDriveFormats[0].ext == std::string_view{"dsk"}
        && kNibbleDriveFormats[0].codec == svc::DiskCodec::Nib,
    "user_io.cpp:2154, :2161, :2177: a .dsk on a nibble drive is typed SD_TYPE_A2"
);
static_assert(
    kNibbleDriveFormats[1].ext == std::string_view{"do"}
        && kNibbleDriveFormats[1].codec == svc::DiskCodec::Nib,
    "user_io.cpp:2162, :2177: a .do is typed SD_TYPE_A2 where stock types it at all"
);
static_assert(
    kNibbleDriveFormats[0].size_bytes == 0
        && kNibbleDriveFormats[0].magic.empty()
        && kNibbleDriveFormats[1].size_bytes == 0
        && kNibbleDriveFormats[1].magic.empty(),
    "user_io.cpp:2161-2162: stock types the image by its extension alone"
);
static_assert(
    kNibbleDriveFormats.back().ext.empty()
        && kNibbleDriveFormats.back().size_bytes == 0
        && kNibbleDriveFormats.back().magic.empty()
        && kNibbleDriveFormats.back().codec == svc::DiskCodec::Raw,
    "the catch-all row is last and carries no rule: a .nib and a .po are the file"
);

consteval const svc::DiskFormatRow& signature_row() {
    for (const svc::DiskFormatRow& row : kSharedDriveFormats)
        if (!row.magic.empty())
            return row;
    return kSharedDriveFormats.back();
}

static_assert(
    signature_row().codec == svc::DiskCodec::Raw
        && signature_row().magic.size() == kExtendedCpcDisk.size()
        && kExtendedCpcDisk.size() <= svc::kDiskMagicProbeBytes,
    "Oric_MiSTer rtl/wd1793.sv:786: the 16-byte EXTENDED CPC DSK signature, inside "
    "the factory's probe, is the file itself"
);
static_assert(
    kSharedDriveFormats.size() == kNibbleDriveFormats.size() + 1
        && kSharedDriveFormats[0].codec == svc::DiskCodec::Nib
        && kSharedDriveFormats[1].codec == svc::DiskCodec::Nib
        && !signature_row().ext.empty()
        && kSharedDriveFormats.back().ext.empty()
        && kSharedDriveFormats.back().codec == svc::DiskCodec::Raw,
    "the signature row sits behind the two extension rows and names an extension, so "
    "the extension pass never takes it for the catch-all"
);

static_assert(
    kTk2000.name == std::string_view{"TK2000"}
        && kTk2000.slots.size() == 2
        && kTk2000.slots[0].index == IoIndex{0}
        && kTk2000.slots[1].index == IoIndex{1}
        && kTk2000.slots[0].extensions == std::string_view{"NIBDSKDO PO "}
        && kTk2000.slots[1].extensions == kTk2000.slots[0].extensions,
    "TK2000_MiSTer CONF_STR: S0,NIBDSKDO PO ; S1,NIBDSKDO PO ; nothing else mounts"
);
static_assert(
    kOric.name == std::string_view{"Oric"}
        && kOric.slots.size() == 4
        && kOric.slots[0].extensions == std::string_view{"NIBDSKDO "}
        && kOric.slots[1].extensions == kOric.slots[0].extensions
        && kOric.slots[2].extensions == std::string_view{"DSK"}
        && kOric.slots[3].extensions == kOric.slots[2].extensions,
    "Oric_MiSTer CONF_STR: h6S0/h6S1,NIBDSKDO ; H6S0..H6S3,DSK"
);
static_assert(
    kOric.slots[0].disk_formats.size() == kSharedDriveFormats.size()
        && kOric.slots[1].disk_formats.size() == kSharedDriveFormats.size()
        && kOric.slots[2].disk_formats.empty()
        && kOric.slots[3].disk_formats.empty(),
    "user_io.cpp:2160: only drives A and B reach the nibble controller "
    "(Oric_MiSTer rtl/pravetz8d_fdc.vhd:15, img_mounted(1 downto 0))"
);
static_assert(
    kTk2000.slots[0].disk_formats.size() == kNibbleDriveFormats.size()
        && kTk2000.slots[1].disk_formats.size() == kNibbleDriveFormats.size(),
    "TK2000_MiSTer rtl/peripheral/floppy_track.sv:103: both drives read 13-block "
    "nibble tracks"
);

consteval bool every_row_is_a_written_drive(std::span<const FileSlot> slots) {
    for (const FileSlot& s : slots)
        if (s.role != SlotRole::Drive || !s.writable || is_load_row(s))
            return false;
    return true;
}

static_assert(
    every_row_is_a_written_drive(kTk2000.slots) && every_row_is_a_written_drive(kOric.slots),
    "menu.cpp:2846-2849 + user_io.cpp:2148-2150: the generic arm, "
    "user_io_file_mount(path, slot) with pre == 0, opened O_RDWR | O_SYNC"
);
static_assert(
    kTk2000.start == StartRule::Chain
        && kOric.start == StartRule::Chain
        && kTk2000.disk_formats.empty()
        && kOric.disk_formats.empty()
        && kTk2000.block_service == BlockService::Generic
        && kOric.block_service == BlockService::Generic,
    "user_io.cpp:1605: both take the generic start arm, and the generic block drain; "
    "the rows are per slot, so the profile declares none"
);

inline constexpr auto kFluxDrive525Formats = std::to_array<svc::DiskFormatRow>({
    {.ext = {},
     .codec = svc::DiskCodec::Woz525,
     .why = "iigs_disk.cpp:185-265: a 5.25-inch WOZ as itself, a 140K disk converted to one"},
});
inline constexpr auto kFluxDrive35Formats = std::to_array<svc::DiskFormatRow>({
    {.ext = {},
     .codec = svc::DiskCodec::Woz35,
     .why = "iigs_disk.cpp:185-265: a 3.5-inch WOZ as itself, an 800K disk converted to one"},
});
inline constexpr auto kBlockSlotFormats = std::to_array<svc::DiskFormatRow>({
    {.ext = {},
     .codec = svc::DiskCodec::ProDosBlocks,
     .why = "iigs_disk.cpp:164-183: ProDOS blocks, a 2MG's or DC42's behind its header"},
});

inline constexpr auto kAppleIISlots = std::to_array<FileSlot>({
    {.index = IoIndex{0},
     .extensions = "NIBDSKDO PO WOZ",
     .label = "Drive 1",
     .writable = true,
     .role = SlotRole::Drive,
     .disk_formats = kFluxDrive525Formats},
    {.index = IoIndex{2},
     .extensions = "NIBDSKDO PO WOZ",
     .label = "Drive 2",
     .writable = true,
     .role = SlotRole::Drive,
     .disk_formats = kFluxDrive525Formats},
});

inline constexpr auto kAppleIIgsSlots = std::to_array<FileSlot>({
    {.index = IoIndex{0},
     .extensions = "HDVPO 2MG",
     .label = "",
     .writable = true,
     .role = SlotRole::Drive,
     .disk_formats = kBlockSlotFormats},
    {.index = IoIndex{1},
     .extensions = "HDVPO 2MG",
     .label = "",
     .writable = true,
     .role = SlotRole::Drive,
     .disk_formats = kBlockSlotFormats},
    {.index = IoIndex{2},
     .extensions = "WOZPO 2MG",
     .label = "WOZ 3.5",
     .writable = true,
     .role = SlotRole::Drive,
     .disk_formats = kFluxDrive35Formats},
    {.index = IoIndex{3},
     .extensions = "WOZDSKDO PO NIB2MG",
     .label = "WOZ 5.25",
     .writable = true,
     .role = SlotRole::Drive,
     .disk_formats = kFluxDrive525Formats},
});

consteval CoreProfile flux_drive_profile(
    CoreKind kind,
    std::string_view name,
    std::span<const FileSlot> slots
) {
    return CoreProfile{
        .kind = kind,
        .name = name,
        .rbf = "",
        .services = {},
        .slots = slots,
        .boot_assets = {},
        .signatures = {},
        .undeclared_start_mount = StartMount::Generic,
        .undeclared_role = SlotRole::Save,
    };
}

inline constexpr CoreProfile kAppleII = flux_drive_profile(
    CoreKind::AppleII,
    "Apple-II",
    kAppleIISlots
);
inline constexpr CoreProfile kAppleIIgs = flux_drive_profile(
    CoreKind::AppleIIgs,
    "Apple-IIgs",
    kAppleIIgsSlots
);

static_assert(
    kFluxDrive525Formats.size() == 1
        && kFluxDrive35Formats.size() == 1
        && kBlockSlotFormats.size() == 1
        && svc::admits_by_content(kFluxDrive525Formats[0].codec)
        && svc::admits_by_content(kFluxDrive35Formats[0].codec)
        && svc::admits_by_content(kBlockSlotFormats[0].codec),
    "iigs_disk.cpp:141-162: the hook runs for every name, so one content-admitting row"
);
static_assert(
    kAppleIIgs.slots[0].disk_formats.data() == kBlockSlotFormats.data()
        && kAppleIIgs.slots[1].disk_formats.data() == kBlockSlotFormats.data()
        && kAppleIIgs.slots[2].disk_formats.data() == kFluxDrive35Formats.data()
        && kAppleIIgs.slots[3].disk_formats.data() == kFluxDrive525Formats.data(),
    "iigs_disk.cpp:30-36 slot_kind: 0 and 1 hard disks, 2 the 3.5-inch, 3 the 5.25-inch"
);
static_assert(
    kAppleIIgs.slots.size() == 4
        && kAppleII.slots.size() == 2
        && kAppleII.slots[0].index == IoIndex{0}
        && kAppleII.slots[1].index == IoIndex{2},
    "Apple-II_MiSTer CONF_STR: S0 and S2 are the Disk II drives; S1, HDV, is the file"
);
static_assert(
    kAppleIIgs.name == std::string_view{"Apple-IIgs"}
        && kAppleII.name == std::string_view{"Apple-II"},
    "iigs_disk.cpp:40-44: the hook keys on CONF_STR field 0 Apple-IIgs"
);
static_assert(
    every_row_is_a_written_drive(kAppleII.slots) && every_row_is_a_written_drive(kAppleIIgs.slots),
    "user_io.cpp:2148-2149: the generic arm opens O_RDWR | O_SYNC where it can"
);
static_assert(
    kAppleIIgs.start == StartRule::Chain
        && kAppleII.start == StartRule::Chain
        && kAppleIIgs.undeclared_start_mount == StartMount::Generic
        && kAppleIIgs.undeclared_role == SlotRole::Save,
    "user_io.cpp:1605-1636: the generic start arm sends boot.rom and boot1.rom; "
    "user_io.cpp:986-1005: the SC4 PRAM image is remembered"
);

}
