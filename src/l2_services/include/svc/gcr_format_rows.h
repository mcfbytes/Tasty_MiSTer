// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "svc/disk_codec.h"
#include "svc/disk_format_row.h"

namespace mister::svc {

inline constexpr std::array<DiskFormatRow, 6> kGcrFormatRows{{
    {.ext = "t64",
     .codec = DiskCodec::T64,
     .why = "user_io.cpp:2128 user_io_file_mount: .t64 is c64_openT64 then c64_openGCR"},
    {.ext = "d64",
     .codec = DiskCodec::GcrSectors,
     .why = "user_io.cpp:2163 user_io_file_mount: .d64 is c64_openGCR"},
    {.ext = "d71",
     .codec = DiskCodec::GcrSectors,
     .why = "user_io.cpp:2165 user_io_file_mount: .d71 is c64_openGCR"},
    {.ext = "g64",
     .codec = DiskCodec::GcrTracks,
     .why = "user_io.cpp:2164 user_io_file_mount: .g64 is c64_openGCR"},
    {.ext = "g71",
     .codec = DiskCodec::GcrTracks,
     .why = "user_io.cpp:2166 user_io_file_mount: .g71 is c64_openGCR"},
    {.ext = {}, .codec = DiskCodec::Raw, .why = "user_io.cpp:2149: every other image is the file"},
}};

}
