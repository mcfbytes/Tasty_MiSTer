// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "proto/types.h"
#include "svc/disc/track.h"
#include "svc/sector_types.h"
#include "svc/types.h"

namespace mister::cores::minimig::cr511 {

inline constexpr std::size_t kFrameMax = 16;

enum class Op : std::uint8_t {
    Sync = 0x00,
    Seek = 0x01,
    Read = 0x02,
    MotorOn = 0x04,
    MotorOff = 0x05,
    PlayLsn = 0x09,
    PlayMsf = 0x0A,
    PlayTrack = 0x0B,
    SyncAlt = 0x80,
    Status = 0x81,
    ErrorInfo = 0x82,
    Model = 0x83,
    ModeSet = 0x84,
    ModeSense = 0x85,
    Capacity = 0x86,
    Subq = 0x87,
    Blank14 = 0x88,
    DiscInfo = 0x89,
    ReadToc = 0x8A,
    Pause = 0x8B,
    Blank4 = 0xA2,
    FrontPanel = 0xA3,
};

inline constexpr infra::ByteCount kTwoByteFrame{2};
inline constexpr infra::ByteCount kSingleByteFrame{1};
inline constexpr infra::ByteCount kSevenByteFrame{7};

using OpRow = std::pair<Op, infra::ByteCount>;
inline constexpr std::array<OpRow, 22> kOps{{
    {Op::Sync, kTwoByteFrame},        {Op::Seek, kSevenByteFrame},
    {Op::Read, kSevenByteFrame},      {Op::MotorOn, kSevenByteFrame},
    {Op::MotorOff, kSevenByteFrame},  {Op::PlayLsn, kSevenByteFrame},
    {Op::PlayMsf, kSevenByteFrame},   {Op::PlayTrack, kSevenByteFrame},
    {Op::SyncAlt, kTwoByteFrame},     {Op::Status, kSingleByteFrame},
    {Op::ErrorInfo, kSevenByteFrame}, {Op::Model, kSevenByteFrame},
    {Op::ModeSet, kSevenByteFrame},   {Op::ModeSense, kSingleByteFrame},
    {Op::Capacity, kSingleByteFrame}, {Op::Subq, kSevenByteFrame},
    {Op::Blank14, kSingleByteFrame},  {Op::DiscInfo, kSevenByteFrame},
    {Op::ReadToc, kSevenByteFrame},   {Op::Pause, kSevenByteFrame},
    {Op::Blank4, kSingleByteFrame},   {Op::FrontPanel, kSevenByteFrame},
}};

[[nodiscard]] constexpr std::optional<Op> op_of(std::byte byte) noexcept {
    for (const OpRow& row : kOps)
        if (std::byte{std::to_underlying(row.first)} == byte)
            return row.first;
    return std::nullopt;
}

[[nodiscard]] constexpr infra::ByteCount frame_bytes(Op op) noexcept {
    for (const OpRow& row : kOps)
        if (row.first == op)
            return row.second;
    return infra::ByteCount{};
}

inline constexpr std::string_view kModel = "MATSHITA0.96";

enum class AudioStatus : std::uint8_t {
    InProgress = 0x11,
    Paused = 0x12,
    PlayComplete = 0x13,
    PlayError = 0x14,
    NoStatus = 0x15,
};

[[nodiscard]] constexpr std::byte status_byte(AudioStatus status) noexcept {
    return std::byte{static_cast<std::uint8_t>(status)};
}

inline constexpr svc::SectorSize kUserData{2048};
inline constexpr svc::SectorSize kEdc{2052};
inline constexpr svc::SectorSize kMode2{2336};
inline constexpr svc::SectorSize kHeaderless{2340};
inline constexpr svc::SectorSize kHalfK{512};
inline constexpr svc::SectorSize kOneK{1024};
inline constexpr svc::SectorSize kRaw{svc::disc::kRawFrameBytes};

inline constexpr std::array<svc::SectorSize, 6> kModeSizes{{
    kHalfK,
    kOneK,
    kUserData,
    kEdc,
    kMode2,
    kHeaderless,
}};

[[nodiscard]] constexpr bool accepted_by_mode_set(svc::SectorSize size) noexcept {
    for (const svc::SectorSize accepted : kModeSizes)
        if (size == accepted)
            return true;
    return false;
}

inline constexpr infra::ByteOffset kSyncBytes{12};
inline constexpr infra::ByteOffset kSyncAndHeaderBytes{16};

struct SectorSlice {
    infra::ByteOffset from;
    infra::ByteCount bytes;
};

[[nodiscard]] constexpr SectorSlice slice_of(svc::SectorSize size) noexcept {
    if (size == kHeaderless)
        return SectorSlice{.from = kSyncBytes, .bytes = size.bytes()};
    if (size == kRaw)
        return SectorSlice{.from = infra::ByteOffset{}, .bytes = size.bytes()};
    return SectorSlice{.from = kSyncAndHeaderBytes, .bytes = size.bytes()};
}

}
