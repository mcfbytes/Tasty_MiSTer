// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <tuple>
#include <utility>

#include "cores/ide/atapi_sense.h"
#include "cores/ide/ide_command.h"
#include "cores/ide/ide_types.h"
#include "hal/types.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"

namespace mister::cores::ide {

struct PacketWords {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr PacketWords of(infra::ByteCount bytes) noexcept {
        constexpr std::size_t kWordBytes = sizeof(std::uint16_t);
        return PacketWords{static_cast<std::uint16_t>((bytes.v + kWordBytes - 1) / kWordBytes)};
    }

    [[nodiscard]] constexpr infra::ByteCount bytes() const noexcept {
        return infra::ByteCount{static_cast<std::size_t>(v) * sizeof(hal::LinkWord)};
    }

    [[nodiscard]] constexpr hal::LinkWord word() const noexcept { return hal::LinkWord{v}; }

    friend constexpr bool operator==(PacketWords, PacketWords) = default;
};

struct IdeTaskFile {

    using Image = std::array<std::byte, 12>;
    using Raw = std::span<const std::byte, std::tuple_size_v<Image>>;

    enum class At : std::size_t {
        FlagsOrBurst = 0,
        FeaturesOrFault = 1,
        Count = 2,
        Sector = 3,
        CylinderLow = 4,
        PacketWordsLow = 8,
        PacketWordsHigh = 9,
        Select = 10,
        CommandOrStatus = 11,
    };

    enum class Fault : std::uint8_t { None = 0x00, DiagnosticPassed = 0x01, Aborted = 0x04 };

    static constexpr std::byte kFastReadFlag{0x02};
    static constexpr std::byte kSelectFixedBits{0xA0};
    static constexpr std::byte kSlaveBit{0x10};
    static constexpr std::byte kLbaBit{0x40};

    static constexpr std::byte kPacketMarker{0x80};

    static constexpr std::byte kTransferMode{0x03};
    static constexpr std::byte kDmaFeature{0x01};

    static constexpr proto::ReplyCount kWords{std::tuple_size_v<Image> / sizeof(hal::LinkWord)};

    bool fast_read = false;
    std::byte features{};
    IdeSectorCount count{};
    IdeAddress address{};
    IdeDriveSelect drive = IdeDriveSelect::Master;
    bool lba = false;
    std::optional<IdeCommand> command{};

    proto::BlockCount burst{};
    Fault fault = Fault::None;
    IdeStatus status{};

    std::optional<PacketWords> packet{};

    std::optional<AtapiSenseKey> packet_sense{};

    [[nodiscard]] constexpr bool dma_feature() const noexcept {
        return (features & kDmaFeature) != std::byte{};
    }

    [[nodiscard]] static constexpr IdeTaskFile decode(Raw raw) noexcept {
        const std::byte select = at(raw, At::Select);
        IdeTaskFile regs{};
        regs.fast_read = (at(raw, At::FlagsOrBurst) & kFastReadFlag) != std::byte{};
        regs.features = at(raw, At::FeaturesOrFault);
        regs.count = IdeSectorCount{std::to_integer<std::uint8_t>(at(raw, At::Count))};
        regs.address = IdeAddress::of(at(raw, At::Sector), cylinder_of(raw), select);
        regs.drive = (select & kSlaveBit) != std::byte{} ? IdeDriveSelect::Slave
                                                         : IdeDriveSelect::Master;
        regs.lba = (select & kLbaBit) != std::byte{};
        regs.command = ide_command_of(at(raw, At::CommandOrStatus));
        return regs;
    }

    [[nodiscard]] static constexpr Image image_of(
        const proto::user_io::PollFrame& block,
        proto::ReplyIndex at
    ) noexcept {
        Image raw{};
        std::span<std::byte> rest{raw};
        for (proto::ReplyCount read{}; read < kWords; read += proto::ReplyCount{1}) {
            block.replies[at + read].store_le(rest.first<sizeof(hal::LinkWord)>());
            rest = rest.subspan(sizeof(hal::LinkWord));
        }
        return raw;
    }

    [[nodiscard]] constexpr Image encode() const noexcept {
        Image raw{};
        at(raw, At::FlagsOrBurst) = burst.low_byte();
        at(raw, At::FeaturesOrFault) = static_cast<std::byte>(fault);
        at(raw, At::Count) = count.byte();
        at(raw, At::Sector) = address.sector_byte();
        address.cylinder_word().store_le(cylinder_of(std::span{raw}));
        at(raw, At::Select) = kSelectFixedBits
            | (lba ? kLbaBit : std::byte{})
            | (drive == IdeDriveSelect::Slave ? kSlaveBit : std::byte{})
            | address.head_byte();
        at(raw, At::CommandOrStatus) = status.byte();
        return raw;
    }

    [[nodiscard]] constexpr Image encode_packet() const noexcept {
        Image raw = encode();
        at(raw, At::FlagsOrBurst) = kPacketMarker;
        if (packet_sense)
            at(raw, At::FeaturesOrFault) = error_register_of(*packet_sense);
        const hal::LinkWord words = packet ? packet->word() : hal::LinkWord{};
        at(raw, At::PacketWordsLow) = words.low_lane();
        at(raw, At::PacketWordsHigh) = std::byte{words.high_byte()};
        return raw;
    }

    friend constexpr bool operator==(const IdeTaskFile&, const IdeTaskFile&) = default;

private:
    template <class Byte>
    [[nodiscard]] static constexpr Byte& at(
        std::span<Byte, std::tuple_size_v<Image>> raw,
        At where
    ) noexcept {
        return raw[std::to_underlying(where)];
    }

    [[nodiscard]] static constexpr std::byte& at(Image& raw, At where) noexcept {
        return raw[std::to_underlying(where)];
    }

    template <class Byte>
    [[nodiscard]] static constexpr std::span<Byte, sizeof(hal::LinkWord)> cylinder_of(
        std::span<Byte, std::tuple_size_v<Image>> raw
    ) noexcept {
        return raw.subspan(std::to_underlying(At::CylinderLow))
            .template first<sizeof(hal::LinkWord)>();
    }
};

}
