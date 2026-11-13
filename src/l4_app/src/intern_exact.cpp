// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/intern_exact.h"

#include <cstdint>
#include <span>

#include "app/link_tx_reader.h"
#include "app/uart_binding.h"
#include "app/uart_persist.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::app {

namespace {

bool read_whole(const svc::Vfs& vfs, std::string_view rel, std::span<std::uint8_t> out) noexcept {
    if (rel.empty() || out.empty() || out.size() > kLinkTxSlabBytes)
        return false;
    auto f = vfs.open(rel, svc::OpenMode::ReadWhole);
    if (!f)
        return false;
    auto sz = (*f)->size();
    if (!sz || sz->v != out.size())
        return false;
    std::size_t off = 0;
    while (off < out.size()) {
        auto n = (*f)->read_at(off, std::as_writable_bytes(out.subspan(off)));
        if (!n || *n == 0)
            return false;
        off += *n;
    }
    return true;
}

}

template <std::size_t N>
    requires(N > 0 && N <= kLinkTxSlabBytes)
std::optional<std::array<std::uint8_t, N>> read_exact(
    const svc::Vfs& vfs,
    std::string_view rel
) noexcept {
    std::array<std::uint8_t, N> buf{};
    if (!read_whole(vfs, rel, buf))
        return std::nullopt;
    return buf;
}

template std::optional<std::array<std::uint8_t, UartPersist::kCardBytes>> read_exact<
    UartPersist::kCardBytes>(const svc::Vfs&, std::string_view) noexcept;

template std::optional<std::array<std::uint8_t, UartBinding::SpeedsFile::kBytes>> read_exact<
    UartBinding::SpeedsFile::kBytes>(const svc::Vfs&, std::string_view) noexcept;

proto::TxSlabId intern_exact(
    const svc::Vfs& vfs,
    LinkTxChannel<SeatTag::Main>& inbox,
    std::string_view rel,
    std::size_t want
) {
    if (rel.empty() || want == 0 || want > kLinkTxSlabBytes)
        return proto::TxSlabId::kNone;
    std::uint8_t buf[kLinkTxSlabBytes]{};
    if (!read_whole(vfs, rel, std::span<std::uint8_t>(buf, want)))
        return proto::TxSlabId::kNone;
    const auto id = inbox.intern(std::span<const std::uint8_t>(buf, want));
    return id ? *id : proto::TxSlabId::kNone;
}

}
