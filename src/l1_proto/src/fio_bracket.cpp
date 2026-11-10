// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/fio_bracket.h"
#include "fio_wire_detail.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "hal/selected.h"
#include "proto/fio_opcodes.h"
#include "proto/link_frame.h"
#include "proto/spi_frame.h"

namespace mister::proto {

using detail::beat;
using detail::kModeDownload;
using detail::kModeEnd;

namespace {

constexpr bool taken(hal::SpiWord) noexcept {
    return true;
}

constexpr std::size_t kExtChars = 4;

constexpr std::uint8_t ascii_upper(std::uint8_t c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<std::uint8_t>(c - 0x20u) : c;
}

[[nodiscard]] Ex<void> write_index_register(ILinkFrame& frame, std::uint16_t index) {
    const std::array<std::uint16_t, 1> words{index};
    return send_frame(frame, FioOp::FileIndex, words);
}

}

Ex<void> FioBracket::set_index(ILinkFrame& frame, IoIndex index) {
    return write_index_register(frame, index.v);
}

Ex<void> FioBracket::set_index(ILinkFrame& frame, WideIoIndex index) {
    return write_index_register(frame, index.v);
}

Ex<void> FioBracket::send_file_info(ILinkFrame& frame, std::string_view ext) {
    std::array<std::uint8_t, kExtChars> c{};
    for (std::size_t i = 0; i < kExtChars && i < ext.size(); ++i) {
        const auto raw = static_cast<std::uint8_t>(ext[i]);
        if (raw == 0)
            break;
        c[i] = ascii_upper(raw);
    }
    const auto w0 = static_cast<std::uint16_t>((c[0] << 8) | c[1]);
    const auto w1 = static_cast<std::uint16_t>((c[2] << 8) | c[3]);

    const std::array<std::uint16_t, 2> words{w0, w1};
    return send_frame(frame, FioOp::FileInfo, words);
}

Ex<FioBracket> FioBracket::begin(
    hal::ISpiTransport& link,
    WideIoIndex index,
    hal::SpiWord mode,
    const SessionParams& params
) {

    SpiFrame frame{link};
    if (auto r = set_index(frame, index); !r)
        return std::unexpected(r.error());
    if (!params.ext.empty()) {
        if (auto r = send_file_info(frame, params.ext); !r) {
            return std::unexpected(r.error());
        }
    }

    std::optional<FioBracket> bracket;
    {
        hal::Selected cs(link, hal::ChipSelect::Fpga);
        if (auto r = link.transfer(word(FioOp::FileTx)); !r)
            return std::unexpected(r.error());

        const bool guarded = mode.v == kModeDownload.v && params.aux.v == 0;
        if (auto r = guarded ? link.transfer_or_post(mode) : beat(link, mode).transform(taken);
            !r) {
            return std::unexpected(r.error());
        }

        bracket.emplace(FioBracket{link, index});

        if (params.aux.v != 0) {
            const auto lo = static_cast<std::uint16_t>(params.aux.v);
            const auto hi = static_cast<std::uint16_t>(
                (params.aux.v >> std::numeric_limits<std::uint16_t>::digits)
            );
            auto r = beat(link, hal::SpiWord{lo});
            if (r)
                r = beat(link, hal::SpiWord{hi});
            if (!r) {
                cs.release();
                (void)bracket->end();
                return std::unexpected(r.error());
            }
        }
    }
    return std::move(*bracket);
}

Ex<FioBracket> FioBracket::window(hal::ISpiTransport& link, WideIoIndex index, hal::SpiWord mode) {
    hal::Selected cs(link, hal::ChipSelect::Fpga);
    if (auto r = link.transfer(word(FioOp::FileTx)); !r)
        return std::unexpected(r.error());
    if (auto r = link.transfer(mode); !r)
        return std::unexpected(r.error());
    return FioBracket{link, index};
}

FioBracket::~FioBracket() {
    if (link_ != nullptr)
        (void)end();
}

Ex<void> FioBracket::end() {
    if (link_ == nullptr)
        return {};

    hal::ISpiTransport* link = std::exchange(link_, nullptr);
    hal::Selected cs(*link, hal::ChipSelect::Fpga);
    if (auto r = link->transfer(word(FioOp::FileTx)); !r)
        return std::unexpected(r.error());
    if (auto r = link->transfer(kModeEnd); !r)
        return std::unexpected(r.error());
    return {};
}

}
