// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/electron_core.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <utility>

#include "svc/gzip_inflate.h"

namespace mister::cores {

namespace {

bool iequal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }
    return true;
}

}

Ex<void> ElectronCore::do_adopt(const CoreAdopt&) {

    return {};
}

void ElectronCore::on_set_pending_file_ext(std::string_view ext) noexcept {
    (void)ext_.assign(ext);
}

Ex<void> ElectronCore::on_file_tx(IoIndex index, std::span<const std::uint8_t> bytes) {
    bytes_sent_ = 0;

    if (iequal(ext_.view(), ".UEF"))
        return tx_uef_(index, bytes);
    return tx_raw_(index, bytes);
}

Ex<std::span<const std::uint8_t>> ElectronCore::materialize_(std::span<const std::uint8_t> bytes) {
    const std::uint64_t total = bytes.size();
    if (total < 2)
        return std::span<const std::uint8_t>{};

    if (bytes[0] == 0x1F && bytes[1] == 0x8B) {
        auto inflated = svc::inflate_gzip(bytes, kMaxImageBytes);
        if (!inflated)
            return std::unexpected(inflated.error());
        if (!inflated->ok)
            return std::span<const std::uint8_t>{};
        image_ = std::move(inflated->data);
        return std::span<const std::uint8_t>{image_};
    }

    if (total > kMaxImageBytes)
        return std::span<const std::uint8_t>{};
    return bytes;
}

Ex<void> ElectronCore::tx_uef_(IoIndex index, std::span<const std::uint8_t> bytes) {
    proto::SessionParams params{};
    params.ext = ext_.view();
    auto session = proto::ImageBracket::open(image_sink(), proto::WideIoIndex{index.v}, params);
    if (!session)
        return std::unexpected(session.error());
    last_index_ = static_cast<std::uint16_t>(index.v);
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
                    val | uef::bit_at(image, cache, ((addr + p) << 3) + bit)
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

    auto session = proto::ImageBracket::open(image_sink(), proto::WideIoIndex{index.v}, params);
    if (!session)
        return std::unexpected(session.error());
    last_index_ = static_cast<std::uint16_t>(index.v);
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
