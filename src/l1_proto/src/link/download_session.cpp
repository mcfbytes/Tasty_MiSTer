// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/download_session.h"
#include "fio_wire_detail.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "hal/selected.h"
#include "infra/prelude.h"
#include "proto/fio_opcodes.h"
#include "proto/wire_word.h"

namespace mister::proto::link {

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
    auto walk = for_each_word(w, data, [&link](hal::SpiWord word) {
        if (auto r = beat(link, word); !r)
            return Ex<bool>{std::unexpected(r.error())};
        return Ex<bool>{true};
    });
    if (!walk)
        return std::unexpected(walk.error());
    return {};
}

}

Ex<void> DownloadSession::set_index(ILinkFrame& frame, FioIndexByte index) {
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

Ex<DownloadSession> DownloadSession::open(
    hal::ISpiTransport& link,
    WideIoIndex index,
    AuxValue address
) {
    auto b = FioBracket::window(link, index, kModeDownload, address);
    if (!b)
        return std::unexpected(b.error());
    return DownloadSession{std::move(*b)};
}

DownloadSession::~DownloadSession() {
    if (dat_held_)
        release_dat();
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
    return for_each_word(link->width(), data, [link](hal::SpiWord word) {
        auto r = link->transfer_or_post(word);
        if (!r)
            return Ex<bool>{std::unexpected(r.error())};
        return Ex<bool>{*r};
    });
}

Ex<void> DownloadSession::post(std::span<const hal::SpiWord> words) {
    hal::ISpiTransport* link = bracket_.link();
    if (link == nullptr || posted_ || words.empty()) {
        return std::unexpected(bracket_.index().refusal(Errc::negotiation, ERR_SITE()));
    }

    link->select(hal::ChipSelect::Fpga);
    Ex<void> r = link->transfer(word(FioOp::FileTxDat)).transform([](hal::SpiWord) noexcept {});
    for (std::size_t i = 0; r && i + 1u < words.size(); ++i)
        r = beat(*link, words[i]).transform([](hal::SpiWord) noexcept {});
    if (r)
        r = link->post(words.back());
    if (!r) {
        link->deselect();
        return r;
    }
    posted_ = true;
    return {};
}

Ex<bool> DownloadSession::settle(std::span<const hal::SpiWord> tail) {
    if (!posted_) {
        return std::unexpected(bracket_.index().refusal(Errc::negotiation, ERR_SITE()));
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
        r = beat(*link, tail[i]).transform([](hal::SpiWord) noexcept {});
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

Ex<void> DownloadSession::open_dat(hal::Width w) {
    hal::ISpiTransport* link = bracket_.link();
    if (link == nullptr || posted_ || dat_held_ || link->posted()) {
        return std::unexpected(bracket_.index().refusal(Errc::negotiation, ERR_SITE()));
    }
    link->select(hal::ChipSelect::Fpga);
    Ex<void> r = link->transfer(word(FioOp::FileTxDat)).transform([](hal::SpiWord) noexcept {});
    if (!r) {
        link->deselect();
        return r;
    }
    dat_ = w;
    dat_held_ = true;
    return {};
}

Ex<void> DownloadSession::feed(std::span<const std::uint8_t> data) {
    hal::ISpiTransport* link = bracket_.link();
    if (link == nullptr || !dat_held_) {
        return std::unexpected(bracket_.index().refusal(Errc::negotiation, ERR_SITE()));
    }
    return emit_payload(*link, data, dat_);
}

void DownloadSession::release_dat() noexcept {
    if (!dat_held_)
        return;
    dat_held_ = false;
    if (hal::ISpiTransport* link = bracket_.link())
        link->deselect();
}

Ex<void> DownloadSession::end() {
    if (posted_) {
        return std::unexpected(bracket_.index().refusal(Errc::would_block, ERR_SITE()));
    }
    release_dat();
    return bracket_.end();
}

Ex<bool> DownloadSession::end_or_post() {
    if (posted_) {
        return std::unexpected(bracket_.index().refusal(Errc::would_block, ERR_SITE()));
    }
    release_dat();
    return bracket_.end_or_post();
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
