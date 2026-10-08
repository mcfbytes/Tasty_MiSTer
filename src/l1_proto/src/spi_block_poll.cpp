// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_block_poll.h"
#include "hal/selected.h"

#include <cstring>
#include <span>

namespace mister::proto {

namespace {

constexpr hal::SpiWord kGetSdStat{0x16};
constexpr hal::SpiWord kSetSdConf{0x19};

constexpr std::uint8_t kCid[16] = {0x3e, 0x00, 0x00, 0x34, 0x38, 0x32, 0x44, 0x00,
                                   0x00, 0x73, 0x2f, 0x6f, 0x93, 0x00, 0xc7, 0xcd};
constexpr std::uint8_t kCsd[16] = {0xf1, 0x40, 0x40, 0x0a, 0x80, 0x7f, 0xe5, 0xe9,
                                   0x00, 0x00, 0x59, 0x5b, 0x32, 0x00, 0x0e, 0x40};

[[nodiscard]] Ex<void> write_framed(hal::ISpiTransport& link, std::span<const std::uint8_t> bytes,
                                    hal::Width width) {
    if (width == hal::Width::Word) {
        for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
            const auto w = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[i]) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[i + 1]) << 8));
            if (auto r = link.transfer(hal::SpiWord{w}); !r) {
                return std::unexpected(r.error());
            }
        }
        if ((bytes.size() & 1u) != 0u) {
            const auto w = static_cast<std::uint16_t>(bytes.back());
            if (auto r = link.transfer(hal::SpiWord{w}); !r) {
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

}  // namespace

IBlockGeometry* SpiBlockPoll::geometry_for_(SlotIndex slot) const noexcept {
    if (!wiring_.roles || slot.v >= kBlockSlots) return nullptr;
    const infra::OptRef<IBlockGeometry> g = (*wiring_.roles)[slot.v].geometry;
    return g ? &*g : nullptr;
}

[[nodiscard]] Ex<void> SpiBlockPoll::send_config(hal::ISpiTransport& link) {
    std::uint8_t csd[sizeof(kCsd)];
    std::memcpy(csd, kCsd, sizeof(csd));
    const std::uint64_t size0 = wiring_.slot0_file_bytes ? *wiring_.slot0_file_bytes : 0;
    csd[6] = static_cast<std::uint8_t>(size0 >> 9);
    csd[7] = static_cast<std::uint8_t>(size0 >> 17);
    csd[8] = static_cast<std::uint8_t>(size0 >> 25);

    hal::Selected cs(link, hal::ChipSelect::Io);

    const hal::Width width = link.width();
    if (auto r = link.transfer(kSetSdConf); !r) return std::unexpected(r.error());
    if (auto r = write_framed(link, kCid, width); !r) return r;
    if (auto r = write_framed(link, csd, width); !r) return r;

    if (auto r = link.transfer(hal::SpiWord{1}); !r) return std::unexpected(r.error());
    return {};
}

[[nodiscard]] Ex<SpiBlockPoll::Decode> SpiBlockPoll::poll(hal::ISpiTransport& link,
                                                          std::optional<SdStatusWord> known) {
    if (known.has_value() && (known->v & 0x8000u) == 0u) {

        known = std::nullopt;
    }

    Decode d{};
    bool stock = false;
    bool config_request = false;
    {

        hal::Selected cs(link, hal::ChipSelect::Io);

        std::uint16_t c = 0;
        if (known.has_value()) {
            c = known->v;
        } else {
            auto r0 = link.transfer(kGetSdStat);
            if (!r0) return std::unexpected(r0.error());
            c = r0->v;
        }

        if ((c & 0x8000u) != 0u) {

            auto w1 = link.transfer(hal::SpiWord{0});
            if (!w1) return std::unexpected(w1.error());
            auto w2 = link.transfer(hal::SpiWord{0});
            if (!w2) return std::unexpected(w2.error());
            auto w3 = link.transfer(hal::SpiWord{0});
            if (!w3) return std::unexpected(w3.error());

            const std::uint32_t lba =
                static_cast<std::uint32_t>(w2->v) | (static_cast<std::uint32_t>(w3->v) << 16);
            const std::uint16_t op = c & 0x0003u;
            d.req.slot = SlotIndex{static_cast<std::uint8_t>((c >> 2) & 0x0Fu)};
            d.req.lba = Lba{lba};
            d.req.block_count = BlockCount{((static_cast<std::uint32_t>(c) >> 9) & 0x3Fu) + 1u};
            d.req.write = (op == 2u);
            d.block_size = 128u << ((static_cast<std::uint32_t>(c) >> 6) & 7u);
            d.ack = static_cast<std::uint16_t>(d.req.slot.v << 8);
            d.decoded = (op != 0u);
        } else {

            auto w1 = link.transfer(hal::SpiWord{0});
            if (!w1) return std::unexpected(w1.error());
            const std::uint16_t rq = w1->v;
            stock = true;
            d.block_size = 512;
            d.req.block_count = BlockCount{1};

            if ((rq & 0x00F0u) == 0x0050u && (rq & 0x3F03u) != 0u) {
                auto w2 = link.transfer(hal::SpiWord{0});
                if (!w2) return std::unexpected(w2.error());
                auto w3 = link.transfer(hal::SpiWord{0});
                if (!w3) return std::unexpected(w3.error());
                d.req.lba = Lba{static_cast<std::uint32_t>(w2->v) |
                                (static_cast<std::uint32_t>(w3->v) << 16)};

                config_request = ((rq & 0x000Cu) == 0x000Cu);

                int disk = -1;
                bool read = false;
                if ((rq & 0x0003u) != 0u) {
                    disk = 0;
                    read = (rq & 0x0001u) != 0u;
                } else if ((rq & 0x0900u) != 0u) {
                    disk = 1;
                    read = (rq & 0x0100u) != 0u;
                } else if ((rq & 0x1200u) != 0u) {
                    disk = 2;
                    read = (rq & 0x0200u) != 0u;
                } else if ((rq & 0x2400u) != 0u) {
                    disk = 3;
                    read = (rq & 0x0400u) != 0u;
                }

                d.ack = (rq & 0x0004u) != 0u ? std::uint16_t{0}
                                             : static_cast<std::uint16_t>((disk + 1) << 8);

                if (disk >= 0) {
                    d.req.slot = SlotIndex{static_cast<std::uint8_t>(disk)};
                    d.req.write = !read;
                    d.decoded = true;
                    ++diag_.stock_requests;
                }
            }

            if (config_request) {
                ++diag_.config_requests;

                if (auto r = send_config(link); !r) return std::unexpected(r.error());
            }
        }
    }

    if (!d.decoded) return d;

    BlockGeometry geo{d.block_size, 0};
    if (IBlockGeometry* const hook = geometry_for_(d.req.slot); hook != nullptr && !stock) {
        geo = hook->geometry_for(d.req.slot, d.req.lba, geo);
    }
    if (geo.block_size == 0) geo.block_size = d.block_size;
    if (geo.block_size > kBlockStagingBytes) {
        ++diag_.oversize_requests;
        geo.block_size = kBlockStagingBytes;
    }

    std::uint32_t blks = d.req.block_count.v;
    if (blks * geo.block_size > kBlockStagingBytes) {
        ++diag_.oversize_requests;
        blks = kBlockStagingBytes / geo.block_size;
        if (blks == 0) blks = 1;
        d.req.block_count = BlockCount{blks};
    }

    std::uint32_t window_blocks = kBlockStagingBytes / geo.block_size;
    if (geo.window_blocks != 0 && geo.window_blocks < window_blocks) {
        window_blocks = geo.window_blocks;
    }
    if (window_blocks < blks) window_blocks = blks;

    d.block_size = geo.block_size;
    d.window_blocks = window_blocks;
    return d;
}

}  // namespace mister::proto
