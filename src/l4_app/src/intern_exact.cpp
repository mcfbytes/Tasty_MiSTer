// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/intern_exact.h"

#include <cstdint>
#include <span>

#include "app/link_tx_reader.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::app {

proto::TxSlabId intern_exact(
    const svc::Vfs& vfs,
    LinkTxChannel<SeatTag::Main>& inbox,
    std::string_view rel,
    std::size_t want
) {
    if (rel.empty() || want == 0 || want > kLinkTxSlabBytes)
        return {};
    auto f = vfs.open(rel, svc::OpenMode::ReadWhole);
    if (!f)
        return {};
    auto sz = (*f)->size();
    if (!sz || sz->v != want)
        return {};
    std::uint8_t buf[kLinkTxSlabBytes]{};
    std::size_t off = 0;
    while (off < want) {
        auto n = (*f)->read_at(
            off,
            std::as_writable_bytes(std::span<std::uint8_t>(buf).subspan(off))
        );
        if (!n || *n == 0)
            return {};
        off += *n;
    }
    const auto id = inbox.intern(std::span<const std::uint8_t>(buf, want));
    return id ? *id : proto::TxSlabId{};
}

}
