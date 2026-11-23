// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/types.h"
#include "proto/disk_address.h"
#include "proto/uio_opcodes.h"

namespace mister::cores::minimig::cd_bridge {

inline constexpr proto::UioOp kWriteOp = proto::UioOp::DmaWrite;
inline constexpr proto::UioOp kReadOp = proto::UioOp::DmaRead;

inline constexpr hal::SpiWord kRead = proto::word(kReadOp);
inline constexpr hal::SpiWord kStatus = proto::word(proto::UioOp::DmaSdio);

inline constexpr proto::DiskAddress kAkikoCommand{0xF400};
inline constexpr proto::DiskAddress kAkikoSubcode{0xF410};
inline constexpr proto::DiskAddress kAkikoNvram{0xF440};
inline constexpr proto::DiskAddress kAkikoSector{0xF500};

inline constexpr proto::DiskAddress kCdda{0xF200};

inline constexpr proto::DiskAddress kCdtvCommand{0xF800};
inline constexpr proto::DiskAddress kCdtvSector{0xF820};
inline constexpr proto::DiskAddress kCdtvStch{0xF840};
inline constexpr proto::DiskAddress kCdtvNvram{0xF880};
inline constexpr proto::DiskAddress kCdtvCard{0xF8C0};

}
