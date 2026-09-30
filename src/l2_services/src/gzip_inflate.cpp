// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/gzip_inflate.h"

#include <array>
#include <cstring>

#include <zlib.h>

namespace mister::svc {

[[nodiscard]] Ex<InflateResult> inflate_gzip(IFile& src, std::size_t cap) {
    InflateResult result;

    z_stream strm{};

    if (inflateInit2(&strm, MAX_WBITS | 16) != Z_OK) return result;

    std::array<std::uint8_t, 16384> in{};
    std::array<std::uint8_t, 16384> chunk{};
    std::uint64_t off = 0;
    bool capped = false;
    int ret = Z_OK;

    for (;;) {
        auto got = src.read_at(off, std::as_writable_bytes(std::span(in)));
        if (!got) {
            inflateEnd(&strm);
            return std::unexpected(got.error());
        }
        if (*got == 0) break;
        off += *got;

        strm.next_in = in.data();
        strm.avail_in = static_cast<uInt>(*got);

        do {
            strm.next_out = chunk.data();
            strm.avail_out = static_cast<uInt>(chunk.size());
            ret = inflate(&strm, Z_NO_FLUSH);

            if (ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                inflateEnd(&strm);
                if (capped) return InflateResult{};
                result.ok = true;
                return result;
            }
            const std::size_t have = chunk.size() - strm.avail_out;
            if (have != 0 && !capped) {
                if (result.data.size() + have > cap) {
                    capped = true;
                } else {
                    result.data.insert(result.data.end(), chunk.begin(), chunk.begin() + have);
                }
            }
        } while (strm.avail_out == 0);

        if (ret == Z_STREAM_END) break;
    }

    inflateEnd(&strm);
    if (capped) return InflateResult{};
    result.ok = true;
    return result;
}

}  // namespace mister::svc
