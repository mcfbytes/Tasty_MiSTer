// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/floppy_write_capture.h"

#include <algorithm>
#include <utility>

#include "infra/endian.h"
#include "infra/span_cut.h"
#include "proto/disk_act.h"

namespace mister::cores::minimig {

namespace {

using Adf = svc::AdfCodec;
using proto::FioWordCount;

constexpr FioWordCount kSyncWordRead{1};
constexpr FioWordCount kFieldWordsRead{5};
constexpr FioWordCount kHeaderRead{Adf::kHeaderWords};
constexpr FioWordCount kDataRead{Adf::kDataWords};
constexpr FioWordCount kHeld{FloppyWriteCapture::kHeldWords};

static_assert(
    FloppyWriteCapture::kHeldWords
    >= Adf::kDataWords - 1 + proto::kDiskActBytes / proto::DiskAct::kWordBytes
);

[[nodiscard]] constexpr FloppyWriteCapture::Error error_of(Adf::HeaderFault f) noexcept {
    using E = FloppyWriteCapture::Error;
    switch (f) {
        case Adf::HeaderFault::None:
            return E::None;
        case Adf::HeaderFault::SecondSync:
            return E::SecondSync;
        case Adf::HeaderFault::Format:
            return E::Format;
        case Adf::HeaderFault::TrackRange:
            return E::TrackRange;
        case Adf::HeaderFault::SectorRange:
            return E::SectorRange;
        case Adf::HeaderFault::GapRange:
            return E::GapRange;
        case Adf::HeaderFault::Checksum:
            return E::HeaderSum;
    }
    return E::None;
}

[[nodiscard]] constexpr FioWordCount words_read(Adf::HeaderFault f) noexcept {
    switch (f) {
        case Adf::HeaderFault::SecondSync:
            return kSyncWordRead;
        case Adf::HeaderFault::Format:
        case Adf::HeaderFault::TrackRange:
        case Adf::HeaderFault::SectorRange:
        case Adf::HeaderFault::GapRange:
            return kFieldWordsRead;
        case Adf::HeaderFault::None:
        case Adf::HeaderFault::Checksum:
            break;
    }
    return kHeaderRead;
}

void compose(Adf::EncodedSector& out, Adf::HeaderWords header, Adf::DataWords data) noexcept {
    constexpr std::size_t kWord = sizeof(std::uint16_t);
    std::size_t at = 0;
    const auto put = [&out, &at](hal::SpiWord w) noexcept {
        const std::span<std::byte, kWord> to = std::span(out).subspan(at).first<kWord>();
        w.store_le(to);
        std::swap(to[0], to[1]);
        at += kWord;
    };
    put(Adf::kFillWord);
    put(Adf::kFillWord);
    put(Adf::kSyncWord);
    for (const hal::SpiWord w : header)
        put(w);
    for (const hal::SpiWord w : data)
        put(w);
}

}

void FloppyWriteCapture::begin(proto::DiskUnit unit, svc::AdfCodec::Track track) noexcept {
    TASTY_SEAT_BODY(FloppyWriteCapture);
    open_ = true;
    unit_ = unit;
    track_ = track;
    stage_ = Stage::Scan;
    held_from_ = FioWordCount{};
    held_end_ = FioWordCount{};
}

std::span<const hal::SpiWord> FloppyWriteCapture::live_() const noexcept {
    return infra::affine::from(infra::affine::first(std::span(held_words_), held_end_), held_from_);
}

void FloppyWriteCapture::take(
    std::span<const hal::SpiWord> words,
    svc::IDiskImage& image
) noexcept {
    TASTY_SEAT_BODY(FloppyWriteCapture);
    if (!open_)
        return;

    std::ranges::copy(live_(), held_words_.begin());
    held_end_ = held_();
    held_from_ = FioWordCount{};
    const std::span<const hal::SpiWord> fits = infra::affine::first_clamped(
        words,
        kHeld - held_end_
    );
    std::ranges::copy(fits, infra::affine::from(std::span(held_words_), held_end_).begin());
    held_end_ += FioWordCount::carrying(proto::FileSize::of(std::as_bytes(fits)), hal::Width::Word);
    while (step_(image)) {
    }
}

void FloppyWriteCapture::end(End how) noexcept {
    TASTY_SEAT_BODY(FloppyWriteCapture);
    if (!open_)
        return;
    if (how == End::Stalled) {
        if (stage_ == Stage::Header)
            fail_(Error::ShortHeader);
        if (stage_ == Stage::Data)
            fail_(Error::ShortData);
    }
    open_ = false;
    stage_ = Stage::Scan;
    held_from_ = FioWordCount{};
    held_end_ = FioWordCount{};
}

bool FloppyWriteCapture::step_(svc::IDiskImage& image) noexcept {
    switch (stage_) {
        case Stage::Scan:
            if (held_() == FioWordCount{})
                return false;
            scan_();
            return true;
        case Stage::Header:
            if (held_() < kHeaderRead)
                return false;
            header_();
            return true;
        case Stage::Data:
            if (held_() < kDataRead)
                return false;
            data_(image);
            return true;
    }
    return false;
}

void FloppyWriteCapture::scan_() noexcept {
    const std::span<const hal::SpiWord> live = live_();
    const auto sync = std::ranges::find(live, Adf::kSyncWord);
    if (sync == live.end()) {
        consume_(held_());
        return;
    }
    const std::span<const hal::SpiWord> read{live.begin(), sync + 1};
    consume_(FioWordCount::carrying(proto::FileSize::of(std::as_bytes(read)), hal::Width::Word));
    stage_ = Stage::Header;
}

void FloppyWriteCapture::header_() noexcept {
    const Adf::HeaderWords words = live_().first<Adf::kHeaderWords>();
    const Adf::Header h = Adf::read_header(words);
    consume_(words_read(h.fault));
    stage_ = Stage::Scan;
    if (h.fault != Adf::HeaderFault::None)
        return fail_(error_of(h.fault));
    if (h.track != track_)
        return fail_(Error::WrongTrack);
    std::copy(words.begin(), words.end(), header_words_.begin());
    sector_ = h.sector;
    stage_ = Stage::Data;
}

void FloppyWriteCapture::data_(svc::IDiskImage& image) noexcept {
    const Adf::DataWords words = live_().first<Adf::kDataWords>();
    consume_(kDataRead);
    stage_ = Stage::Scan;
    Adf::SectorFile decoded{};
    if (!Adf::read_data(words, decoded))
        return fail_(Error::DataSum);
    if (!image.writable())
        return fail_(Error::Protected);
    Adf::EncodedSector sector{};
    compose(sector, header_words_, words);
    auto put = image.write_at(Adf::sector_at(track_, sector_), sector);
    if (!put) {
        io_faults_.add();
        return;
    }
    written_.add();
}

void FloppyWriteCapture::fail_(Error e) noexcept {
    last_error_ = e;
    errors_.add();
}

}
