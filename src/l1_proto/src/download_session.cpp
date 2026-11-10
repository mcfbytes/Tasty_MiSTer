// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/download_session.h"
#include "fio_wire_detail.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "hal/selected.h"
#include "infra/endian.h"
#include "proto/fio_opcodes.h"

namespace mister::proto {

using detail::beat;
using detail::kModeDownload;

namespace {

constexpr auto kEmptyCheatSet = std::to_array<std::uint8_t>({0, 0});
constexpr IoIndex kCheatIndex{255};

[[nodiscard]] Ex<void> emit_payload(
    hal::ISpiTransport& link,
    std::span<const std::uint8_t> data,
    hal::Width w
) {
    if (w == hal::Width::Word) {
        const std::size_t pairs = data.size() / 2u;
        for (std::size_t i = 0; i < pairs; ++i) {
            const auto pair_word = infra::load_le<std::uint16_t>(data.subspan(2u * i).first<2>());
            if (auto r = beat(link, hal::SpiWord{pair_word}); !r)
                return std::unexpected(r.error());
        }
        if ((data.size() & 1u) != 0u) {
            const auto tail = static_cast<std::uint16_t>(data.back());
            if (auto r = beat(link, hal::SpiWord{tail}); !r)
                return std::unexpected(r.error());
        }
        return {};
    }
    for (const std::uint8_t byte : data) {
        if (auto r = beat(link, hal::SpiWord{byte}); !r)
            return std::unexpected(r.error());
    }
    return {};
}

}

Ex<void> DownloadSession::set_index(ILinkFrame& frame, IoIndex index) {
    return FioBracket::set_index(frame, index);
}

Ex<void> DownloadSession::set_index(ILinkFrame& frame, WideIoIndex index) {
    return FioBracket::set_index(frame, index);
}

Ex<void> DownloadSession::send_file_info(ILinkFrame& frame, std::string_view ext) {
    return FioBracket::send_file_info(frame, ext);
}

Ex<DownloadSession> DownloadSession::begin(hal::ISpiTransport& link, IoIndex index) {
    return begin(link, WideIoIndex::of(index, 0), SessionParams{});
}

Ex<DownloadSession> DownloadSession::begin(
    hal::ISpiTransport& link,
    IoIndex index,
    const SessionParams& params
) {
    return begin(link, WideIoIndex::of(index, 0), params);
}

Ex<DownloadSession> DownloadSession::begin(
    hal::ISpiTransport& link,
    WideIoIndex index,
    const SessionParams& params
) {
    auto b = FioBracket::begin(link, index, kModeDownload, params);
    if (!b)
        return std::unexpected(b.error());
    return DownloadSession{std::move(*b)};
}

Ex<DownloadSession> DownloadSession::open(hal::ISpiTransport& link, WideIoIndex index) {
    auto b = FioBracket::window(link, index, kModeDownload);
    if (!b)
        return std::unexpected(b.error());
    return DownloadSession{std::move(*b)};
}

DownloadSession::~DownloadSession() {
    if (posted_)
        abandon();
}

Ex<void> DownloadSession::write(std::span<const std::uint8_t> data) {
    hal::ISpiTransport* link = bracket_.link();
    if (link == nullptr)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    return write(data, link->width());
}

Ex<void> DownloadSession::write(std::span<const std::uint8_t> data, hal::Width w) {
    hal::ISpiTransport* link = bracket_.link();
    if (link == nullptr) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    hal::Selected cs(*link, hal::ChipSelect::Fpga);
    if (auto r = link->transfer(word(FioOp::FileTxDat)); !r)
        return std::unexpected(r.error());
    return emit_payload(*link, data, w);
}

Ex<std::size_t> DownloadSession::write_taken(std::span<const std::uint8_t> data) {
    hal::ISpiTransport* link = bracket_.link();
    if (link == nullptr)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    if (link->posted())
        return 0;
    hal::Selected cs(*link, hal::ChipSelect::Fpga);
    auto head = link->transfer_or_post(word(FioOp::FileTxDat));
    if (!head)
        return std::unexpected(head.error());
    if (!*head)
        return 0;
    const std::size_t step = link->width() == hal::Width::Word ? 2u : 1u;
    for (std::size_t at = 0; at < data.size(); at += step) {
        auto out_word = static_cast<std::uint16_t>(data[at]);
        if (step == 2u && at + 1u < data.size())
            out_word = infra::load_le<std::uint16_t>(data.subspan(at).first<2>());
        auto r = link->transfer_or_post(hal::SpiWord{out_word});
        if (!r)
            return std::unexpected(r.error());
        if (!*r)
            return std::min(at + step, data.size());
    }
    return data.size();
}

Ex<void> DownloadSession::post(std::span<const std::uint16_t> words) {
    hal::ISpiTransport* link = bracket_.link();
    if (link == nullptr || posted_ || words.empty()) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), bracket_.index().fio_byte()});
    }

    link->select(hal::ChipSelect::Fpga);
    Ex<void> r = link->transfer(word(FioOp::FileTxDat)).transform([](hal::SpiWord) noexcept {});
    for (std::size_t i = 0; r && i + 1u < words.size(); ++i)
        r = beat(*link, hal::SpiWord{words[i]}).transform([](hal::SpiWord) noexcept {});
    if (r)
        r = link->post(hal::SpiWord{words.back()});
    if (!r) {
        link->deselect();
        return r;
    }
    posted_ = true;
    return {};
}

Ex<bool> DownloadSession::settle(std::span<const std::uint16_t> tail) {
    if (!posted_) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), bracket_.index().fio_byte()});
    }
    hal::ISpiTransport* link = bracket_.link();
    auto done = link->posted_done();
    if (!done) {
        abandon();
        return std::unexpected(done.error());
    }
    if (!*done)
        return false;
    posted_ = false;
    Ex<void> r{};
    for (std::size_t i = 0; r && i < tail.size(); ++i)
        r = beat(*link, hal::SpiWord{tail[i]}).transform([](hal::SpiWord) noexcept {});
    link->deselect();
    if (!r)
        return std::unexpected(r.error());
    return true;
}

void DownloadSession::abandon() noexcept {
    if (!posted_)
        return;
    posted_ = false;
    hal::ISpiTransport* link = bracket_.link();
    link->abandon_post();
    link->deselect();
    bracket_.detach();
}

Ex<void> DownloadSession::end() {
    if (posted_) {
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), bracket_.index().fio_byte()});
    }
    return bracket_.end();
}

Ex<void> DownloadSession::send_cheats(
    hal::ISpiTransport& link,
    std::span<const std::uint8_t> table
) {
    auto session = begin(link, kCheatIndex);
    if (!session)
        return std::unexpected(session.error());

    const std::span<const std::uint8_t> payload = table.empty()
        ? std::span<const std::uint8_t>{kEmptyCheatSet}
        : table;
    if (auto r = session->write(payload); !r) {
        return r;
    }
    return session->end();
}

}
