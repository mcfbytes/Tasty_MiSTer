// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/upload_session.h"
#include "fio_wire_detail.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "hal/selected.h"
#include "infra/endian.h"
#include "proto/fio_opcodes.h"

namespace mister::proto {

using detail::beat;
using detail::kModeUpload;

namespace {

[[nodiscard]] Ex<void> absorb_payload(
    hal::ISpiTransport& link,
    std::span<std::uint8_t> data,
    hal::Width w
) {
    if (w == hal::Width::Word) {
        const std::size_t pairs = data.size() / 2u;
        for (std::size_t i = 0; i < pairs; ++i) {
            auto r = beat(link, hal::SpiWord{0});
            if (!r)
                return std::unexpected(r.error());
            infra::store_le<std::uint16_t>(data.subspan(2u * i).first<2>(), r->v);
        }
        if ((data.size() & 1u) != 0u) {
            auto r = beat(link, hal::SpiWord{0});
            if (!r)
                return std::unexpected(r.error());
            data.back() = static_cast<std::uint8_t>(r->v & 0x00FFu);
        }
        return {};
    }
    for (std::uint8_t& byte : data) {
        auto r = beat(link, hal::SpiWord{0});
        if (!r)
            return std::unexpected(r.error());
        byte = static_cast<std::uint8_t>(r->v & 0x00FFu);
    }
    return {};
}

}

Ex<UploadSession> UploadSession::begin(hal::ISpiTransport& link, WideIoIndex index) {
    auto b = FioBracket::begin(link, index, kModeUpload, SessionParams{});
    if (!b)
        return std::unexpected(b.error());
    return UploadSession{std::move(*b)};
}

Ex<void> UploadSession::read(std::span<std::uint8_t> data) {
    hal::ISpiTransport* link = bracket_.link();
    if (link == nullptr)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    return read(data, link->width());
}

Ex<void> UploadSession::read(std::span<std::uint8_t> data, hal::Width w) {
    hal::ISpiTransport* link = bracket_.link();
    if (link == nullptr) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    hal::Selected cs(*link, hal::ChipSelect::Fpga);
    if (auto r = link->transfer(word(FioOp::FileTxDat)); !r)
        return std::unexpected(r.error());
    return absorb_payload(*link, data, w);
}

}
