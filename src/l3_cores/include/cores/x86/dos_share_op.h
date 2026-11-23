// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace mister::cores::x86 {

enum class DosShareOp : std::uint8_t {
    Rmdir = 0x01,
    Mkdir = 0x03,
    Chdir = 0x05,
    Close = 0x06,
    Read = 0x08,
    Write = 0x09,
    Lock = 0x0A,
    DiskSpace = 0x0C,
    SetAttr = 0x0E,
    GetAttr = 0x0F,
    Rename = 0x11,
    Delete = 0x13,
    Open = 0x16,
    Create = 0x17,
    FindFirst = 0x1B,
    FindNext = 0x1C,
    SkfMend = 0x21,
    Qualify = 0x23,
    SpOpen = 0x2E,
};

[[nodiscard]] constexpr std::optional<DosShareOp> dos_share_op_of(std::byte code) noexcept {
    switch (static_cast<DosShareOp>(code)) {
        case DosShareOp::Rmdir:
            return DosShareOp::Rmdir;
        case DosShareOp::Mkdir:
            return DosShareOp::Mkdir;
        case DosShareOp::Chdir:
            return DosShareOp::Chdir;
        case DosShareOp::Close:
            return DosShareOp::Close;
        case DosShareOp::Read:
            return DosShareOp::Read;
        case DosShareOp::Write:
            return DosShareOp::Write;
        case DosShareOp::Lock:
            return DosShareOp::Lock;
        case DosShareOp::DiskSpace:
            return DosShareOp::DiskSpace;
        case DosShareOp::SetAttr:
            return DosShareOp::SetAttr;
        case DosShareOp::GetAttr:
            return DosShareOp::GetAttr;
        case DosShareOp::Rename:
            return DosShareOp::Rename;
        case DosShareOp::Delete:
            return DosShareOp::Delete;
        case DosShareOp::Open:
            return DosShareOp::Open;
        case DosShareOp::Create:
            return DosShareOp::Create;
        case DosShareOp::FindFirst:
            return DosShareOp::FindFirst;
        case DosShareOp::FindNext:
            return DosShareOp::FindNext;
        case DosShareOp::SkfMend:
            return DosShareOp::SkfMend;
        case DosShareOp::Qualify:
            return DosShareOp::Qualify;
        case DosShareOp::SpOpen:
            return DosShareOp::SpOpen;
    }
    return std::nullopt;
}

}
