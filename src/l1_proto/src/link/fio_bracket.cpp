// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/fio_bracket.h"
#include "fio_wire_detail.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "hal/selected.h"
#include "infra/prelude.h"
#include "proto/fio_opcodes.h"
#include "proto/link_frame.h"
#include "proto/link/fio_bracket_words.h"
#include "proto/link/spi_frame.h"

namespace mister::proto::link {

using detail::beat;
using detail::kModeDownload;
using detail::kModeEnd;

namespace {

constexpr bool taken(hal::LinkWord) noexcept {
    return true;
}

}

Ex<void> FioBracket::set_index(ILinkFrame& frame, FioIndexByte index) {
    return FioBracketWords::set_index(frame, index);
}

Ex<void> FioBracket::set_index(ILinkFrame& frame, WideIoIndex index) {
    return FioBracketWords::set_index(frame, index);
}

Ex<void> FioBracket::send_file_info(ILinkFrame& frame, std::string_view ext) {
    return FioBracketWords::send_file_info(frame, ext);
}

Ex<FioBracket> FioBracket::begin(
    hal::ISpiTransport& link,
    WideIoIndex index,
    hal::LinkWord mode,
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

        const bool guarded = mode == kModeDownload && params.aux.empty();

        const auto put = [&](hal::LinkWord w) -> Ex<void> {
            if (bracket)
                return beat(link, w).transform([](hal::LinkWord) noexcept {});
            auto r = guarded ? link.transfer_or_post(w) : beat(link, w).transform(taken);
            if (!r)
                return std::unexpected(r.error());
            bracket.emplace(FioBracket{link, index});
            return {};
        };
        if (auto r = FioBracketWords::for_window_words(mode, params.aux, put); !r) {
            if (bracket) {
                cs.release();
                (void)bracket->end();
            }
            return std::unexpected(r.error());
        }
    }
    return std::move(*bracket);
}

Ex<FioBracket> FioBracket::window(
    hal::ISpiTransport& link,
    WideIoIndex index,
    hal::LinkWord mode,
    AuxValue address
) {
    hal::Selected cs(link, hal::ChipSelect::Fpga);
    if (auto r = link.transfer(word(FioOp::FileTx)); !r)
        return std::unexpected(r.error());
    if (auto r = FioBracketWords::for_window_words(
            mode,
            address,
            [&link](hal::LinkWord w) -> Ex<void> {
                if (auto t = link.transfer(w); !t)
                    return std::unexpected(t.error());
                return {};
            }
        );
        !r) {
        return std::unexpected(r.error());
    }
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

Ex<bool> FioBracket::end_or_post() {
    if (link_ == nullptr)
        return true;
    hal::ISpiTransport* link = std::exchange(link_, nullptr);
    hal::Selected cs(*link, hal::ChipSelect::Fpga);
    if (auto r = link->transfer(word(FioOp::FileTx)); !r)
        return std::unexpected(r.error());
    return link->transfer_or_post(kModeEnd);
}

}
