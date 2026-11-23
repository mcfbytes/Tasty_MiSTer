// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <span>

#include "svc/disk_codec.h"
#include "svc/disk_format_row.h"
#include "svc/flat_sector_image.h"

namespace mister::cores::atari8 {

inline constexpr auto kSioDiskFormats = std::to_array<svc::DiskFormatRow>({
    {.magic = std::span{svc::FlatSectorImage::kAtrMagic},
     .codec = svc::DiskCodec::Raw,
     .why = "atari800.cpp:1496,1527-1540: an ATR's 16-byte header gives its sectors"},
    {.ext = "xfd",
     .codec = svc::DiskCodec::Raw,
     .why = "atari800.cpp:1508-1513: an XFD is 128-byte sectors from byte 0"},
    {.ext = "xdf",
     .codec = svc::DiskCodec::Raw,
     .why = "Atari800.sv's S6 list spells the XFD extension XDF"},
    {.ext = "atx", .codec = svc::DiskCodec::Atx, .why = "atari800.cpp:1514-1520: an ATX"},
});

inline constexpr auto kSioHardDiskFormats = std::to_array<svc::DiskFormatRow>({
    {.ext = {},
     .codec = svc::DiskCodec::Raw,
     .why = "atari800.cpp:1542-1555: the hard disk is the file, in 512-byte sectors"},
});

}
