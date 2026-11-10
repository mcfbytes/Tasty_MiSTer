// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/gzip_inflate.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

#include <zlib.h>

namespace mister::svc {

namespace {

constexpr std::uint8_t kMagicFirst = 0x1F;
constexpr std::uint8_t kMagicSecond = 0x8B;

constexpr std::size_t kMagicBytes = 2;

constexpr int kGzipWindowBits = MAX_WBITS | 16;

}

class GzipInflater::Stream {
public:
    Stream() noexcept = default;

    ~Stream() {
        if (built_)
            inflateEnd(&strm_);
    }

    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

    [[nodiscard]] bool begin() noexcept {

        if (!built_) {
            built_ = inflateInit2(&strm_, kGzipWindowBits) == Z_OK;
            return built_;
        }
        return inflateReset2(&strm_, kGzipWindowBits) == Z_OK;
    }

    [[nodiscard]] Ex<std::vector<std::uint8_t>> run(
        std::span<const std::uint8_t> src,
        proto::FileSize cap
    );

private:
    z_stream strm_{};
    bool built_ = false;
};

Ex<std::vector<std::uint8_t>> GzipInflater::Stream::run(
    std::span<const std::uint8_t> src,
    proto::FileSize cap
) {
    std::vector<std::uint8_t> out;

    std::array<std::uint8_t, 16384> in{};
    std::array<std::uint8_t, 16384> chunk{};
    proto::FileSize total{};
    std::uint64_t off = 0;
    bool capped = false;
    int ret = Z_OK;

    for (;;) {
        if (off >= src.size())
            break;
        const std::size_t got = std::min(in.size(), src.size() - static_cast<std::size_t>(off));
        std::memcpy(in.data(), src.data() + off, got);
        off += got;

        strm_.next_in = in.data();
        strm_.avail_in = static_cast<uInt>(got);

        do {
            strm_.next_out = chunk.data();
            strm_.avail_out = static_cast<uInt>(chunk.size());

            ret = ::inflate(&strm_, Z_NO_FLUSH);

            if (ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                if (capped)
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
                return out;
            }
            const std::size_t have = chunk.size() - strm_.avail_out;
            if (have != 0 && !capped) {
                const proto::FileSize next = total + proto::FileSize{have};
                if (next > cap) {
                    capped = true;
                } else {
                    out.insert(out.end(), chunk.begin(), chunk.begin() + have);
                    total = next;
                }
            }
        } while (strm_.avail_out == 0);

        if (ret == Z_STREAM_END)
            break;
    }

    if (capped)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    return out;
}

GzipInflater::GzipInflater() noexcept = default;
GzipInflater::~GzipInflater() = default;

bool GzipInflater::is_gzip(std::span<const std::uint8_t> src) noexcept {
    return src.size() >= kMagicBytes && src[0] == kMagicFirst && src[1] == kMagicSecond;
}

Ex<std::vector<std::uint8_t>> GzipInflater::inflate(
    std::span<const std::uint8_t> src,
    proto::FileSize cap
) {
    TASTY_SEAT_BODY(GzipInflater);
    if (!stream_)
        stream_ = std::make_unique<Stream>();

    if (!stream_->begin())
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    return stream_->run(src, cap);
}

}
