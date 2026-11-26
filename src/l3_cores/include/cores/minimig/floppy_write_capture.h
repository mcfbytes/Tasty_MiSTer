// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "hal/types.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/types.h"
#include "svc/adf_codec.h"
#include "svc/disk_image.h"

namespace mister::cores::minimig {

class FloppyWriteCapture {
    TASTY_SEAT_RESIDENT(Io);

public:
    enum class Error : std::uint8_t {
        None = 0,
        ShortHeader = 20,
        SecondSync = 21,
        Format = 22,
        TrackRange = 23,
        SectorRange = 24,
        GapRange = 25,
        HeaderSum = 26,
        WrongTrack = 27,
        ShortData = 28,
        DataSum = 29,
        Protected = 30,
    };

    enum class End : std::uint8_t { Withdrawn, Stalled };

    static constexpr std::size_t kHeldWords = 1024;

    void begin(proto::DiskUnit unit, svc::AdfCodec::Track track) noexcept;

    [[nodiscard]] bool open() const noexcept { return open_; }

    [[nodiscard]] bool writes(proto::DiskUnit unit, svc::AdfCodec::Track track) const noexcept {
        return open_ && unit_ == unit && track_ == track;
    }

    [[nodiscard]] bool on(proto::DiskUnit unit) const noexcept { return open_ && unit_ == unit; }

    [[nodiscard]] proto::DiskUnit unit() const noexcept { return unit_; }

    void take(std::span<const hal::LinkWord> words, svc::IDiskImage& image) noexcept;

    void end(End how) noexcept;

    [[nodiscard]] Error last_error() const noexcept { return last_error_; }

    [[nodiscard]] infra::Tally errors() const noexcept { return errors_; }

    [[nodiscard]] infra::Tally sectors_written() const noexcept { return written_; }

    [[nodiscard]] infra::Tally io_faults() const noexcept { return io_faults_; }

private:
    enum class Stage : std::uint8_t { Scan, Header, Data };

    [[nodiscard]] bool step_(svc::IDiskImage& image) noexcept;
    void scan_() noexcept;
    void header_() noexcept;
    void data_(svc::IDiskImage& image) noexcept;
    void fail_(Error e) noexcept;

    [[nodiscard]] proto::FioWordCount held_() const noexcept { return held_end_ - held_from_; }

    void consume_(proto::FioWordCount words) noexcept { held_from_ += words; }

    [[nodiscard]] std::span<const hal::LinkWord> live_() const noexcept;

    bool open_ = false;
    proto::DiskUnit unit_{};
    svc::AdfCodec::Track track_{};
    Stage stage_ = Stage::Scan;
    svc::AdfCodec::Sector sector_{};
    std::array<hal::LinkWord, kHeldWords> held_words_{};
    proto::FioWordCount held_from_{};
    proto::FioWordCount held_end_{};

    std::array<hal::LinkWord, svc::AdfCodec::kHeaderWords> header_words_{};

    Error last_error_ = Error::None;
    infra::Tally errors_{};
    infra::Tally written_{};
    infra::Tally io_faults_{};
};

}
