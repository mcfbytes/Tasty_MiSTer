// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/file_tx.h"

#include "proto/image_bracket.h"

#include <algorithm>

namespace mister::cores {

[[nodiscard]] Ex<std::uint64_t> stream_file_tx(proto::IImageSink& sink, proto::WideIoIndex index,
                                               const proto::SessionParams& params, svc::IFile& f,
                                               std::span<std::uint8_t> buf,
                                               FileTxCounters* counters) {
    auto total = f.size();
    if (!total) return std::unexpected(total.error());
    if (buf.empty()) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});

    auto session = proto::ImageBracket::open(sink, index, params);
    if (!session) return std::unexpected(session.error());
    if (counters != nullptr) counters->opened = true;

    std::uint64_t remaining = total->v;
    std::uint64_t off = 0;
    while (remaining != 0) {
        const std::size_t chunk =
            static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buf.size()));
        auto got = f.read_at(off, std::as_writable_bytes(buf.subspan(0, chunk)));
        if (!got) return std::unexpected(got.error());
        if (*got == 0) {

            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(off)});
        }
        if (auto r = session->write(std::span<const std::uint8_t>(buf.data(), *got)); !r) {
            return std::unexpected(r.error());
        }
        if (counters != nullptr) ++counters->chunks;
        off += *got;
        remaining -= *got;
    }
    if (auto r = session->end(); !r) return std::unexpected(r.error());
    return off;
}

}  // namespace mister::cores
