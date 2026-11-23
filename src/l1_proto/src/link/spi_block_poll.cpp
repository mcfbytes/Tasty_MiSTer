// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/spi_block_poll.h"
#include "hal/selected.h"
#include "infra/prelude.h"
#include "hal/spi_halves.h"
#include "proto/block_server.h"
#include "proto/uio_opcodes.h"
#include "proto/wire_word.h"

#include <array>
#include <span>

namespace mister::proto::link {

namespace {

constexpr auto kCid = std::to_array<std::uint8_t>(
    {0x3e, 0x00, 0x00, 0x34, 0x38, 0x32, 0x44, 0x00, 0x00, 0x73, 0x2f, 0x6f, 0x93, 0x00, 0xc7, 0xcd}
);
constexpr auto kCsd = std::to_array<std::uint8_t>(
    {0xf1, 0x40, 0x40, 0x0a, 0x80, 0x7f, 0xe5, 0xe9, 0x00, 0x00, 0x59, 0x5b, 0x32, 0x00, 0x0e, 0x40}
);

enum CsdLayout : unsigned {
    kCsdSizeByte0 = 6,
    kCsdSizeByte1 = 7,
    kCsdSizeByte2 = 8,
    kSdhcPermanently = 1,
    kStockBlockBytes = 512,
};

inline constexpr hal::SpiWord kStockTagMask{0x00F0};
inline constexpr hal::SpiWord kStockTag{0x0050};
inline constexpr hal::SpiWord kStockActive{0x3F03};
inline constexpr hal::SpiWord kStockConfig{0x000C};
inline constexpr hal::SpiWord kStockNoAck{0x0004};
inline constexpr hal::SpiWord kStockDisk0{0x0003};
inline constexpr hal::SpiWord kStockDisk0Read{0x0001};
inline constexpr hal::SpiWord kStockDisk1{0x0900};
inline constexpr hal::SpiWord kStockDisk1Read{0x0100};
inline constexpr hal::SpiWord kStockDisk2{0x1200};
inline constexpr hal::SpiWord kStockDisk2Read{0x0200};
inline constexpr hal::SpiWord kStockDisk3{0x2400};
inline constexpr hal::SpiWord kStockDisk3Read{0x0400};

struct StockDiskRow {
    hal::SpiWord any;
    hal::SpiWord read;
    DiskUnit unit;
};

inline constexpr std::array<StockDiskRow, 4> kStockDisks{{
    {kStockDisk0, kStockDisk0Read, DiskUnit{0}},
    {kStockDisk1, kStockDisk1Read, DiskUnit{1}},
    {kStockDisk2, kStockDisk2Read, DiskUnit{2}},
    {kStockDisk3, kStockDisk3Read, DiskUnit{3}},
}};

[[nodiscard]] Ex<void> write_framed(
    hal::ISpiTransport& link,
    std::span<const std::uint8_t> bytes,
    hal::Width width
) {
    auto walk = for_each_word(width, bytes, [&link](hal::SpiWord word) {
        if (auto r = link.transfer(word); !r)
            return Ex<bool>{std::unexpected(r.error())};
        return Ex<bool>{true};
    });
    if (!walk)
        return std::unexpected(walk.error());
    return {};
}

}

[[nodiscard]] Ex<void> SpiBlockPoll::send_config() {
    hal::ISpiTransport& link = wiring_.link;
    const FileSize size0 = wiring_.slot0_file_bytes ? *wiring_.slot0_file_bytes : FileSize{};
    std::array<std::uint8_t, kCsd.size()> csd = kCsd;
    const auto [csize0, csize1, csize2] = size0.csd_size_bytes();
    csd[kCsdSizeByte0] = csize0;
    csd[kCsdSizeByte1] = csize1;
    csd[kCsdSizeByte2] = csize2;

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
            c = SdStatusWord::of(*r0);
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
            d.req.lba = Lba::of(hal::SpiHalves{.low = *w2, .high = *w3});
            d.req.block_count = c.block_count();
            d.req.write = c.writes();
            d.block_size = c.block_size();
            d.ack = SectorAck::of(c.slot());
            d.decoded = c.decoded();
        } else {

            auto w1 = link.transfer(hal::SpiWord{0});
            if (!w1)
                return std::unexpected(w1.error());
            const hal::SpiWord rq = *w1;
            stock = true;
            d.block_size = BlockSize{kStockBlockBytes};
            d.req.block_count = BlockCount{1};

            if (rq.masked(kStockTagMask) == kStockTag && rq.intersects(kStockActive)) {
                auto w2 = link.transfer(hal::SpiWord{0});
                if (!w2)
                    return std::unexpected(w2.error());
                auto w3 = link.transfer(hal::SpiWord{0});
                if (!w3)
                    return std::unexpected(w3.error());
                d.req.lba = Lba::of(hal::SpiHalves{.low = *w2, .high = *w3});

                config_request = rq.contains(kStockConfig);

                std::optional<DiskUnit> disk;
                bool read = false;
                for (const StockDiskRow& row : kStockDisks) {
                    if (!rq.intersects(row.any))
                        continue;
                    disk = row.unit;
                    read = rq.intersects(row.read);
                    break;
                }

                const bool no_ack = rq.intersects(kStockNoAck);
                d.ack = no_ack || !disk ? SectorAck{} : SectorAck::stock(*disk);

                if (disk) {
                    d.req.slot = disk->slot();
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
        } else if (const auto window = backing.window()) {
            geo = window->geometry_for(d.req.slot, d.req.lba, geo);
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
    const BlockCount staging_blocks = size.blocks_in(FileSize{kBlockStagingBytes});
    if (!size.empty() && blks > staging_blocks) {
        ++diag_.oversize_requests;
        blks = staging_blocks;
        if (blks == BlockCount{})
            blks = BlockCount{1};
        d.req.block_count = blks;
    }

    BlockCount window_blocks = staging_blocks;
    const BlockCount geo_window = geo.window_blocks;
    if (geo_window != BlockCount{} && geo_window < window_blocks)
        window_blocks = geo_window;
    if (window_blocks < blks)
        window_blocks = blks;

    d.block_size = size;
    d.window_blocks = window_blocks;
    d.policy = geo.policy;
    return d;
}

}
