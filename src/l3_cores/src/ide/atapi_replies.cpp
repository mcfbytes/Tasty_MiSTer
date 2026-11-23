// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/ide/atapi_replies.h"

#include <algorithm>
#include <array>
#include <bit>
#include <string_view>

#include "cores/ide/atapi_reply_writer.h"
#include "infra/byte_offset.h"
#include "infra/endian.h"
#include "infra/indexed_span.h"
#include "infra/span_cut.h"
#include "svc/disc/track.h"
#include "svc/msf.h"

namespace mister::cores::ide {

namespace {

using svc::TrackCount;
using svc::TrackIndex;
using svc::disc::Track;
using svc::disc::TrackType;

template <unsigned... B>
consteval std::array<std::byte, sizeof...(B)> image() {
    return {std::byte{B}...};
}

constexpr infra::ByteCount kBlank{8};

constexpr infra::ByteCount kTocEntryBytes{8};
constexpr std::byte kOneSession{0x01};
constexpr std::byte kLeadOutTrack{0xAA};

constexpr std::byte kUnreadEntryControl = AtapiToc::adr_control(TrackType::Mode1);

constexpr infra::ByteCount kCapacity{2 * sizeof(std::uint32_t)};

constexpr infra::ByteOffset kSubLengthAt{2};
constexpr std::byte kSubPositionFormat{0x01};
constexpr std::byte kIndexPregap{0x00};
constexpr std::byte kIndexProgram{0x01};

constexpr infra::ByteCount kModeHeaderBytes{8};
constexpr auto kErrorRecoveryPage = image<0x01, 0x06, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00>();
constexpr auto kAudioControlPage = image<
    0x0E,
    0x0E,
    0x04,
    0x00,
    0x00,
    0x00,
    0x00,
    75,
    0x01,
    0xFF,
    0x02,
    0xFF,
    0x00,
    0x00,
    0x00,
    0x00>();
constexpr infra::ByteOffset kPort0VolumeAt{9};
constexpr infra::ByteOffset kPort1VolumeAt{11};

constexpr auto kCapabilitiesPage = image<
    0x2A,
    0x0E,
    0x07,
    0x00,
    0x71,
    0xFF,
    0x2F,
    0x03,
    0x05,
    0x80,
    0x01,
    0x00,
    0x06,
    0x00,
    0x05,
    0x80>();
constexpr std::byte kStubPageLength{0x06};
constexpr infra::ByteCount kStubPageBody{6};

constexpr infra::ByteCount kInquiryBytes{47};
constexpr infra::ByteOffset kInquiryExtraAt{4};
constexpr infra::ByteCount kInquiryHeaderBytes{5};

constexpr auto kInquiryHead = image<0x05, 0x80, 0x00, 0x21>();

constexpr infra::ByteOffset kInquiryTextAt{8};
constexpr std::string_view kInquiryText = "MiSTer  CDROM           ";
constexpr auto kInquiry = [] {
    std::array<std::byte, kInquiryBytes.size()> block{};
    std::ranges::copy(kInquiryHead, block.begin());
    const std::span<std::byte> text = infra::affine::from(
        std::span<std::byte>{block},
        kInquiryTextAt
    );
    std::ranges::fill(text, std::byte{' '});
    for (std::size_t i = 0; i < kInquiryText.size(); ++i)
        text[i] = std::bit_cast<std::byte>(kInquiryText[i]);
    return block;
}();

constexpr auto kConfiguration = image<
    0x00,
    0x00,
    0x00,
    0x0F,
    0x00,
    0x00,
    0x00,
    0x08,
    0x00,
    0x00,
    0x03,
    0x04,
    0x00,
    0x08,
    0x01,
    0x00>();

constexpr auto kDiscInformation = image<
    0x00,
    0x20,
    0x0E,
    0x01,
    0x01,
    0x01,
    0x00,
    0x20,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00>();
constexpr infra::ByteOffset kDiscRowsAt{6};

constexpr auto kTrackInformation = image<
    0x00,
    0x14,
    0x00,
    0x01,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00>();
constexpr infra::ByteOffset kTrackRowAt{2};
constexpr infra::ByteOffset kTrackModeAt{6};
constexpr infra::ByteOffset kTrackStartAt{8};
constexpr std::byte kTrackMode1{0x01};
constexpr std::byte kTrackMode2{0x02};

[[nodiscard]] infra::ByteCount blank(std::span<std::byte> out) noexcept {
    std::ranges::fill(infra::affine::first(out, kBlank), std::byte{0});
    return kBlank;
}

template <std::size_t N>
[[nodiscard]] infra::ByteCount place(
    const std::array<std::byte, N>& image_bytes,
    infra::ByteCount alloc,
    std::span<std::byte> out
) noexcept {
    std::ranges::copy(image_bytes, out.begin());
    return std::min(alloc, infra::ByteCount{N});
}

void put_toc_entry(
    AtapiReplyWriter& w,
    const AtapiToc& toc,
    svc::TrackNumber number,
    bool as_msf
) noexcept {
    const std::optional<AtapiToc::Entry> entry = toc.entry(number);
    w.put(std::byte{0});
    w.put(entry ? AtapiToc::adr_control(entry->type) : kUnreadEntryControl);
    w.put(number);
    w.put(std::byte{0});
    w.put_address(entry ? AtapiReplyWriter::msf_of(entry->start) : svc::Msf{}, as_msf);
}

void put_lead_out(AtapiReplyWriter& w, proto::Lba lead_out, bool as_msf) noexcept {
    w.put(std::byte{0});
    w.put(AtapiToc::adr_control(TrackType::Mode1));
    w.put(kLeadOutTrack);
    w.put(std::byte{0});
    w.put_address(AtapiReplyWriter::msf_of(lead_out), as_msf);
}

}

infra::ByteCount write_toc(
    const AtapiToc& toc,
    const AtapiPacket& packet,
    std::span<std::byte> out
) noexcept {
    const std::optional<AtapiToc::Bounds> bounds = toc.bounds();
    const std::optional<AtapiTocFormat> format = packet.toc_format();
    if (!bounds || !format)
        return blank(out);

    const bool as_msf = packet.msf();
    AtapiReplyWriter w{out};
    w.put_zeros(AtapiReplyWriter::kLengthBytes);
    switch (*format) {
        case AtapiTocFormat::Session:
            w.put(kOneSession);
            w.put(kOneSession);
            put_toc_entry(w, toc, bounds->first, as_msf);
            break;
        case AtapiTocFormat::Toc: {

            const infra::ByteCount room = packet.alloc();
            const auto fits = [&w, room] {
                return w.written() + kTocEntryBytes <= room;
            };
            w.put(bounds->first);
            w.put(bounds->last);
            const TrackIndex last = bounds->last.index();
            for (TrackIndex index = bounds->first.index(); index <= last; index += TrackCount{1}) {
                const svc::TrackNumber number = index.number();
                if (number >= packet.toc_start()) {
                    if (!fits())
                        break;
                    put_toc_entry(w, toc, number, as_msf);
                }
                if (index == last)
                    break;
            }
            if (fits())
                put_lead_out(w, bounds->lead_out, as_msf);
            break;
        }
    }
    w.patch_length(infra::ByteOffset{});
    return w.written();
}

infra::ByteCount write_capacity(const AtapiToc& toc, std::span<std::byte> out) noexcept {
    const std::span<std::byte> reply = infra::affine::first(out, kCapacity);
    toc.lead_out().store_be32(reply.first<sizeof(std::uint32_t)>());
    infra::store_be<std::uint32_t>(reply.last<sizeof(std::uint32_t)>(), svc::disc::kUserDataBytes);
    return kCapacity;
}

infra::ByteCount write_subchannel(
    const AtapiToc& toc,
    const AtapiPacket& packet,
    AtapiAudioStatus status,
    std::optional<proto::Lba> head,
    std::span<std::byte> out
) noexcept {
    if (!packet.sub_position() || !head)
        return blank(out);
    const std::optional<AtapiToc::Hit> hit = toc.row_at(*head);
    if (!hit)
        return blank(out);
    const Track row = toc.row(hit->row);

    std::ranges::fill(infra::affine::first(out, kBlank), std::byte{0});
    AtapiReplyWriter w{out};
    w.put(std::byte{0});
    w.put(wire_byte(status));
    w.put_zeros(AtapiReplyWriter::kLengthBytes);
    if (packet.sub_q()) {
        const bool as_msf = packet.msf();
        const proto::BlockCount into = *head >= row.start ? *head - row.start : row.start - *head;
        w.put(kSubPositionFormat);
        w.put(AtapiToc::adr_control(row.type));
        w.put(row.number);
        w.put(hit->pregap ? kIndexPregap : kIndexProgram);
        w.put_address(AtapiReplyWriter::msf_of(*head), as_msf);
        w.put_address(AtapiReplyWriter::msf_of(into), as_msf);
    }
    w.patch_length(kSubLengthAt);
    return w.written();
}

infra::ByteCount write_mode_pages(
    AtapiPacket::PageSelect page,
    AtapiVolume port0,
    std::span<std::byte> out
) noexcept {
    AtapiReplyWriter w{out};
    w.put_zeros(kModeHeaderBytes);
    bool built = false;
    if (page.selects(AtapiModePage::ErrorRecovery)) {
        built = true;
        w.put(std::span<const std::byte>{kErrorRecoveryPage});
    }
    if (page.selects(AtapiModePage::AudioControl)) {
        built = true;
        std::array audio = kAudioControlPage;
        const infra::IndexedSpan<std::byte, infra::ByteOffset> levels{std::span<std::byte>{audio}};
        levels[kPort0VolumeAt] = port0.level_byte();
        levels[kPort1VolumeAt] = port0.level_byte();
        w.put(std::span<const std::byte>{audio});
    }
    if (page.selects(AtapiModePage::Capabilities)) {
        built = true;
        w.put(std::span<const std::byte>{kCapabilitiesPage});
    }
    if (!built) {
        w.put(page.page_byte());
        w.put(kStubPageLength);
        w.put_zeros(kStubPageBody);
    }
    w.patch_length(infra::ByteOffset{});
    return w.written();
}

infra::ByteCount write_inquiry(infra::ByteCount alloc, std::span<std::byte> out) noexcept {
    std::ranges::fill(infra::affine::first(out, std::max(alloc, kInquiryBytes)), std::byte{0});
    std::ranges::copy(kInquiry, out.begin());

    infra::IndexedSpan<std::byte, infra::ByteOffset>{out}[kInquiryExtraAt] = std::byte{
        proto::FileSize::of(alloc - kInquiryHeaderBytes).file_word<std::uint8_t>()
    };
    return alloc;
}

infra::ByteCount write_configuration(infra::ByteCount alloc, std::span<std::byte> out) noexcept {
    return place(kConfiguration, alloc, out);
}

infra::ByteCount write_disc_information(
    const AtapiToc& toc,
    infra::ByteCount alloc,
    std::span<std::byte> out
) noexcept {
    const infra::ByteCount len = place(kDiscInformation, alloc, out);
    AtapiReplyWriter rows{infra::affine::from(out, kDiscRowsAt)};
    rows.put(toc.rows());
    return len;
}

infra::ByteCount write_track_information(
    const AtapiToc& toc,
    TrackIndex row,
    infra::ByteCount alloc,
    std::span<std::byte> out
) noexcept {
    const infra::ByteCount len = place(kTrackInformation, alloc, out);
    const Track track = toc.row(row);
    const infra::IndexedSpan<std::byte, infra::ByteOffset> field{out};
    AtapiReplyWriter(infra::affine::from(out, kTrackRowAt)).put(row);
    field[kTrackModeAt] = track.type == TrackType::Mode2 ? kTrackMode2 : kTrackMode1;

    std::array<std::byte, sizeof(std::uint32_t)> start{};
    track.start.store_be32(std::span{start});
    field[kTrackStartAt] = start.front();
    return len;
}

}
