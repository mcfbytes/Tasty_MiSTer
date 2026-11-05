// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_block_poll.h"
#include "hal/selected.h"
#include "proto/block_server.h"
#include "proto/uio_opcodes.h"

#include <cstring>
#include <span>

namespace mister::proto {

namespace {

constexpr std::uint8_t kCid[16] = {
    0x3e,
    0x00,
    0x00,
    0x34,
    0x38,
    0x32,
    0x44,
    0x00,
    0x00,
    0x73,
    0x2f,
    0x6f,
    0x93,
    0x00,
    0xc7,
    0xcd
};
constexpr std::uint8_t kCsd[16] = {
    0xf1,
    0x40,
    0x40,
    0x0a,
    0x80,
    0x7f,
    0xe5,
    0xe9,
    0x00,
    0x00,
    0x59,
    0x5b,
    0x32,
    0x00,
    0x0e,
    0x40
};

enum CsdLayout : unsigned {
    kCsdSizeByte0 = 6,
    kCsdSizeByte1 = 7,
    kCsdSizeByte2 = 8,
    kCsdSizeShift0 = 9,
    kCsdSizeShift1 = 17,
    kCsdSizeShift2 = 25,
    kByteBits = 8,
    kPairBytes = 2,
    kSdhcPermanently = 1,
    kStockBlockBytes = 512,
};

enum StockRequest : std::uint16_t {
    kStockTagMask = 0x00F0,
    kStockTag = 0x0050,
    kStockActive = 0x3F03,
    kStockConfig = 0x000C,
    kStockNoAck = 0x0004,
    kStockDisk0 = 0x0003,
    kStockDisk0Read = 0x0001,
    kStockDisk1 = 0x0900,
    kStockDisk1Read = 0x0100,
    kStockDisk2 = 0x1200,
    kStockDisk2Read = 0x0200,
    kStockDisk3 = 0x2400,
    kStockDisk3Read = 0x0400,
    kDisk1 = 1,
    kDisk2 = 2,
    kDisk3 = 3,
    kStockAckShift = 8,
    kLbaHalfBits = 16,
    kAckSlotShift = 8,
};

[[nodiscard]] Ex<void> write_framed(
    hal::ISpiTransport& link,
    std::span<const std::uint8_t> bytes,
    hal::Width width
) {
    if (width == hal::Width::Word) {
        for (std::size_t i = 0; i + 1 < bytes.size(); i += kPairBytes) {
            const std::uint16_t low = bytes[i];
            const std::uint16_t high = bytes[i + 1];
            const hal::SpiWord w{static_cast<std::uint16_t>(low | (high << kByteBits))};
            if (auto r = link.transfer(w); !r) {
                return std::unexpected(r.error());
            }
        }
        if ((bytes.size() & 1u) != 0u) {
            if (auto r = link.transfer(hal::SpiWord{std::uint16_t{bytes.back()}}); !r) {
                return std::unexpected(r.error());
            }
        }
        return {};
    }
    for (const std::uint8_t b : bytes) {
        if (auto r = link.transfer(hal::SpiWord{b}); !r) {
            return std::unexpected(r.error());
        }
    }
    return {};
}

Lba lba_of_words(hal::SpiWord low, hal::SpiWord high) noexcept {
    return Lba{
        static_cast<std::uint32_t>(low.v) | (static_cast<std::uint32_t>(high.v) << kLbaHalfBits)
    };
}

}

[[nodiscard]] Ex<void> SpiBlockPoll::send_config() {
    hal::ISpiTransport& link = wiring_.link;
    std::uint8_t csd[sizeof(kCsd)];
    std::memcpy(csd, kCsd, sizeof(csd));
    const std::uint64_t size0 = wiring_.slot0_file_bytes ? *wiring_.slot0_file_bytes : 0;
    csd[kCsdSizeByte0] = static_cast<std::uint8_t>(size0 >> kCsdSizeShift0);
    csd[kCsdSizeByte1] = static_cast<std::uint8_t>(size0 >> kCsdSizeShift1);
    csd[kCsdSizeByte2] = static_cast<std::uint8_t>(size0 >> kCsdSizeShift2);

    hal::Selected cs(link, hal::ChipSelect::Io);

    const hal::Width width = link.width();
    if (auto r = link.transfer(word(UioOp::SetSdConf)); !r)
        return std::unexpected(r.error());
    if (auto r = write_framed(link, kCid, width); !r)
        return r;
    if (auto r = write_framed(link, csd, width); !r)
        return r;

    if (auto r = link.transfer(hal::SpiWord{kSdhcPermanently}); !r)
        return std::unexpected(r.error());
    return {};
}

[[nodiscard]] Ex<SpiBlockPoll::Decode> SpiBlockPoll::poll(
    IBlockRoles& roles,
    const SlotServers& servers,
    std::optional<SdStatusWord> known
) {
    hal::ISpiTransport& link = wiring_.link;
    if (known.has_value() && !known->valid()) {

        known = std::nullopt;
    }

    Decode d{};
    bool stock = false;
    bool config_request = false;
    {

        hal::Selected cs(link, hal::ChipSelect::Io);

        SdStatusWord c{};
        if (known.has_value()) {
            c = *known;
        } else {
            auto r0 = link.transfer(word(UioOp::GetSdStat));
            if (!r0)
                return std::unexpected(r0.error());
            c = SdStatusWord{r0->v};
        }

        if (c.valid()) {

            auto w1 = link.transfer(hal::SpiWord{0});
            if (!w1)
                return std::unexpected(w1.error());
            auto w2 = link.transfer(hal::SpiWord{0});
            if (!w2)
                return std::unexpected(w2.error());
            auto w3 = link.transfer(hal::SpiWord{0});
            if (!w3)
                return std::unexpected(w3.error());

            d.req.slot = c.slot();
            d.req.lba = lba_of_words(*w2, *w3);
            d.req.block_count = c.block_count();
            d.req.write = c.writes();
            d.block_size = c.block_size();
            d.ack = static_cast<std::uint16_t>(c.slot().v << kAckSlotShift);
            d.decoded = c.decoded();
        } else {

            auto w1 = link.transfer(hal::SpiWord{0});
            if (!w1)
                return std::unexpected(w1.error());
            const std::uint16_t rq = w1->v;
            stock = true;
            d.block_size = BlockSize{kStockBlockBytes};
            d.req.block_count = BlockCount{1};

            if ((rq & kStockTagMask) == kStockTag && (rq & kStockActive) != 0u) {
                auto w2 = link.transfer(hal::SpiWord{0});
                if (!w2)
                    return std::unexpected(w2.error());
                auto w3 = link.transfer(hal::SpiWord{0});
                if (!w3)
                    return std::unexpected(w3.error());
                d.req.lba = lba_of_words(*w2, *w3);

                config_request = ((rq & kStockConfig) == kStockConfig);

                int disk = -1;
                bool read = false;
                if ((rq & kStockDisk0) != 0u) {
                    disk = 0;
                    read = (rq & kStockDisk0Read) != 0u;
                } else if ((rq & kStockDisk1) != 0u) {
                    disk = kDisk1;
                    read = (rq & kStockDisk1Read) != 0u;
                } else if ((rq & kStockDisk2) != 0u) {
                    disk = kDisk2;
                    read = (rq & kStockDisk2Read) != 0u;
                } else if ((rq & kStockDisk3) != 0u) {
                    disk = kDisk3;
                    read = (rq & kStockDisk3Read) != 0u;
                }

                d.ack = (rq & kStockNoAck) != 0u
                    ? std::uint16_t{0}
                    : static_cast<std::uint16_t>((disk + 1) << kStockAckShift);

                if (disk >= 0) {
                    d.req.slot = SlotIndex{static_cast<std::uint8_t>(disk)};
                    d.req.write = !read;
                    d.decoded = true;
                    ++diag_.stock_requests;
                }
            }

            if (config_request) {
                ++diag_.config_requests;

                if (auto r = send_config(); !r)
                    return std::unexpected(r.error());
            }
        }
    }

    if (!d.decoded)
        return d;

    BlockGeometry geo{d.block_size, BlockCount{}};
    if (!stock) {
        const BlockBacking backing = roles.resolve(slot_server_of(servers, d.req.slot));
        if (const auto server = backing.server()) {
            geo = server->geometry_for(d.req.slot, d.req.lba, geo);
        }
    }
    BlockSize size = geo.block_size;
    if (size.empty())
        size = d.block_size;
    if (size > BlockSize{kBlockStagingBytes}) {
        ++diag_.oversize_requests;
        size = BlockSize{kBlockStagingBytes};
    }

    BlockCount blks = d.req.block_count;
    if (size.span_of(blks) > BlockSize{kBlockStagingBytes}) {
        ++diag_.oversize_requests;
        blks = size.blocks_in(BlockSize{kBlockStagingBytes});
        if (blks == BlockCount{})
            blks = BlockCount{1};
        d.req.block_count = blks;
    }

    BlockCount window_blocks = size.blocks_in(BlockSize{kBlockStagingBytes});
    const BlockCount geo_window = geo.window_blocks;
    if (geo_window != BlockCount{} && geo_window < window_blocks)
        window_blocks = geo_window;
    if (window_blocks < blks)
        window_blocks = blks;

    d.block_size = size;
    d.window_blocks = window_blocks;
    return d;
}

}
