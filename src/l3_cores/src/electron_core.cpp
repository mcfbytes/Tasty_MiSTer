// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/electron_core.h"
#include "cores/core_adopt.h"
#include "infra/ascii.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "svc/gzip_inflate.h"

namespace mister::cores {

Ex<void> ElectronCore::do_adopt(const CoreAdopt&) {

    return {};
}

void ElectronCore::on_set_pending_file_ext(std::string_view ext) noexcept {
    (void)ext_.assign(ext);
}

Ex<void> ElectronCore::on_file_tx(IoIndex index, std::span<const std::uint8_t> bytes) {
    bytes_sent_ = 0;

    if (infra::ieq(ext_.view(), ".UEF"))
        return tx_uef_(index, bytes);
    return tx_raw_(index, bytes);
}

Ex<std::span<const std::uint8_t>> ElectronCore::materialize_(std::span<const std::uint8_t> bytes) {
    const std::uint64_t total = bytes.size();
    if (total < 2)
        return std::span<const std::uint8_t>{};

    if (svc::GzipInflater::is_gzip(bytes))
        return std::span<const std::uint8_t>{};

    if (total > kMaxImageBytes)
        return std::span<const std::uint8_t>{};
    return bytes;
}

Ex<void> ElectronCore::tx_uef_(IoIndex index, std::span<const std::uint8_t> bytes) {
    proto::SessionParams params{};
    params.ext = ext_.view();
    auto session = proto::ImageBracket::open(
        image_sink(),
        proto::WideIoIndex::of(index, 0),
        params
    );
    if (!session)
        return std::unexpected(session.error());
    last_index_ = index;
    ++transfers_;
    last_numbits_ = 0;

    auto materialized = materialize_(bytes);
    if (!materialized)
        return std::unexpected(materialized.error());
    const std::span<const std::uint8_t> image = *materialized;
    if (image.empty() || !uef::valid_header(image)) {
        return session->end();
    }

    const std::uint32_t numbits = uef::total_bits(image);
    last_numbits_ = numbits;
    const std::uint32_t size = (numbits + 7) / 8;

    uef::ChunkInfo cache{};
    std::uint32_t addr = 0;
    while (addr < size) {
        const std::uint32_t chunk = std::min<std::uint32_t>(kChunkBytes, size - addr);
        for (std::uint32_t p = 0; p < chunk; ++p) {
            std::uint8_t val = 0;
            for (unsigned bit = 0; bit < 8; ++bit) {
                val = static_cast<std::uint8_t>(val << 1);
                val = static_cast<std::uint8_t>(
                    val | uef::bit_at(image, cache, uef::TapeBit{((addr + p) << 3) + bit})
                );
            }
            buf_[p] = val;
        }
        if (auto r = session->write(std::span<const std::uint8_t>(buf_, chunk)); !r)
            return r;
        bytes_sent_ += chunk;
        addr += chunk;
    }
    return session->end();
}

Ex<void> ElectronCore::tx_raw_(IoIndex index, std::span<const std::uint8_t> bytes) {
    proto::SessionParams params{};
    params.ext = ext_.view();

    auto session = proto::ImageBracket::open(
        image_sink(),
        proto::WideIoIndex::of(index, 0),
        params
    );
    if (!session)
        return std::unexpected(session.error());
    last_index_ = index;
    ++transfers_;

    for (std::size_t off = 0; off < bytes.size();) {
        const std::size_t chunk = std::min<std::size_t>(bytes.size() - off, kChunkBytes);
        std::memcpy(buf_, bytes.data() + off, chunk);
        if (auto r = session->write(std::span<const std::uint8_t>(buf_, chunk)); !r)
            return r;
        bytes_sent_ += chunk;
        off += chunk;
    }
    return session->end();
}

}
