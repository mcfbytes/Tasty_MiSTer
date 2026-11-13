// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "infra/error.h"

namespace mister::fw {

class TastyRefusal {
public:
    enum class Why : std::uint8_t {
        NoVerb = 1,
        NoMovie,
        MovieTooLong,
        NoRecAction,
        BadRecAction,
        UnknownVerb,
        BadRom,
        BadCore,
        BadRecord,
        BadLead,
        BadLinger,
        BadVsyncAdjust,
        BadStopAt,
        BadRamInit,
        BadPhase,
        UnknownOption,
        CheckNoRom,
        BadCodec,
        BadScale,
        BadEvery,
        BadFrom,
        BadTo,
        BadSegment,
        BadRecBounds,
        RecOptionsWithoutRecord,
        BadMotion,
        SliceOnRecStart,
        BadSave,
        SaveNotPlay,

        LoopWithRecord,
        NoCodec,
        RamInitUnsupported,
        MovieUnreadable,
        LeadOutOfRange,
        PhaseOutOfRange,
        RomNeedsChecksum,
        RomPathTooLong,
        RomNotFound,
        RomSearchStopped,
        RomUnplayable,
        RomUnhashable,
        MenuLeftRunning,
        RecordNoTarget,
        RecordIsDirectory,
        RecordNoStem,
        RecordParentFailed,
        RecordIsFile,
        RecordDirFailed,
        RecordTooLong,
        kCount,
    };
    using enum Why;

    constexpr TastyRefusal(Why why) noexcept : why_(why) {}

    [[nodiscard]] constexpr Why why() const noexcept { return why_; }

    [[nodiscard]] constexpr std::uint32_t detail() const noexcept {
        return kTag | static_cast<std::uint32_t>(why_);
    }

    [[nodiscard]] constexpr Error error(infra::ErrorSite at) const noexcept {
        return Error{Errc::bad_format, at, detail()};
    }

    [[nodiscard]] static constexpr std::optional<TastyRefusal> of(const Error& e) noexcept {
        if (e.code != Errc::bad_format)
            return std::nullopt;
        const std::uint32_t ordinal = e.detail ^ kTag;
        if (ordinal == 0 || ordinal >= std::to_underlying(Why::kCount))
            return std::nullopt;
        return TastyRefusal{static_cast<Why>(ordinal)};
    }

    [[nodiscard]] const char* sentence() const noexcept;

    void say() const noexcept;

    [[nodiscard]] static const char* sentence_for(const Error& e) noexcept;

private:
    static constexpr std::uint32_t kTag = 0x74610000;
    Why why_;
};

}
