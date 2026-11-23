// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/intern_exact.h"
#include "infra/span_cut.h"

#include <cstdint>
#include <span>

#include "app/link_tx_reader.h"
#include "app/uart_binding.h"
#include "app/uart_persist.h"
#include "infra/prelude.h"
#include "proto/status_word.h"
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
    if (!sz || *sz != proto::FileSize::of(out))
        return false;
    return (*f)->read_exact_at(proto::kFileStart, std::as_writable_bytes(out)).has_value();
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

template std::optional<std::array<std::uint8_t, sizeof(proto::StatusWord::Bytes)>> read_exact<
    sizeof(proto::StatusWord::Bytes)>(const svc::Vfs&, std::string_view) noexcept;

}
