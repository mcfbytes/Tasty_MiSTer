// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/upload_session.h"
#include "fio_wire_detail.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "hal/selected.h"
#include "infra/prelude.h"
#include "proto/fio_opcodes.h"
#include "proto/wire_word.h"

namespace mister::proto::link {

using detail::beat;
using detail::kModeUpload;

namespace {

[[nodiscard]] Ex<void> absorb_payload(
    hal::ISpiTransport& link,
    std::span<std::uint8_t> data,
    hal::Width w
) {
    return fill_from_words(w, data, [&link] { return beat(link, hal::LinkWord{0}); });
}

}

Ex<UploadSession> UploadSession::begin(hal::ISpiTransport& link, WideIoIndex index) {
    auto b = FioBracket::begin(link, index, kModeUpload, SessionParams{});
    if (!b)
        return std::unexpected(b.error());
    return UploadSession{std::move(*b)};
}

Ex<UploadSession> UploadSession::begin(
    hal::ISpiTransport& link,
    WideIoIndex index,
    const SessionParams& params
) {
    auto b = FioBracket::begin(link, index, kModeUpload, params);
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
