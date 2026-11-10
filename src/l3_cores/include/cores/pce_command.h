// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/types.h"
#include "proto/types.h"
#include "svc/msf.h"
#include "svc/toc.h"
#include "svc/track.h"

namespace mister::cores {

class PceCommand {
public:
    enum : std::size_t {
        kOpcodeAt = 0,
        kModeAt = 1,
        kStartHighAt = 2,
        kStartMidAt = 3,
        kStartLowAt = 4,
        kStartLow2At = 5,
        kAddressingAt = 9,
        kFlagsAt = 13,
        kBytes = 14,
        kWords = 7,
    };

    enum : unsigned { kHighShift = 16, kMidShift = 8 };

    enum : std::uint8_t { kAddressingMask = 0xC0, kFastSeekFlag = 0x80 };

    enum Opcode : std::uint8_t {
        TestUnit = 0x00,
        RequestSense = 0x03,
        Read6 = 0x08,
        ModeSelect6 = 0x15,
        Sapsp = 0xD8,
        Sapep = 0xD9,
        Pause = 0xDA,
        ReadSubq = 0xDD,
        GetDirInfo = 0xDE,
    };

    enum Addressing : std::uint8_t { LbaMode = 0x00, MsfMode = 0x40, TrackMode = 0x80 };

    enum class CddaMode : std::uint8_t { Silent = 0x00, Loop = 0x01, Interrupt = 0x02 };

    enum class DirInfo : std::uint8_t { Summary = 0x00, DiscEnd = 0x01, Track = 0x02 };

    struct ReadOperands {
        HeadLba head{};
        SectorCount count{};
        friend constexpr bool operator==(ReadOperands, ReadOperands) = default;
    };

    [[nodiscard]] static PceCommand of(std::span<const std::uint8_t, kBytes> raw) noexcept;

    [[nodiscard]] std::optional<Opcode> opcode() const noexcept;

    [[nodiscard]] ReadOperands read6() const noexcept;

    [[nodiscard]] HeadLba target(bool clamp_track, const svc::Toc& toc) const noexcept;

    [[nodiscard]] svc::TrackNumber clamped_track(const svc::Toc& toc) const noexcept;

    [[nodiscard]] bool fast_seek() const noexcept;

    [[nodiscard]] std::optional<CddaMode> cdda_mode() const noexcept;

    [[nodiscard]] std::optional<DirInfo> dir_info() const noexcept;

    [[nodiscard]] bool requests_data() const noexcept;

private:
    [[nodiscard]] static constexpr std::optional<CddaMode> cdda_mode_of(
        std::uint8_t byte
    ) noexcept {
        switch (byte) {
            case std::to_underlying(CddaMode::Silent):
                return CddaMode::Silent;
            case std::to_underlying(CddaMode::Loop):
                return CddaMode::Loop;
            case std::to_underlying(CddaMode::Interrupt):
                return CddaMode::Interrupt;
            default:
                return std::nullopt;
        }
    }

    std::optional<Opcode> opcode_{};
    Addressing addressing_ = LbaMode;
    std::optional<CddaMode> cdda_mode_{};
    std::optional<DirInfo> dir_info_{};
    bool fast_seek_ = false;
    bool data_request_ = false;
    HeadLba read_head_{};
    SectorCount read_count_{};
    svc::Msf msf_{};
    proto::PackedBcd track_bcd_{};
    HeadLba raw_head_{};
};

}
