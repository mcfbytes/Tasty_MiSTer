// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/download_session.h"
#include "hal/selected.h"

#include <optional>
#include <utility>

namespace mister::proto {

namespace {

constexpr hal::SpiWord kFileTx{0x53};
constexpr hal::SpiWord kFileTxDat{0x54};
constexpr hal::SpiWord kFileIndex{0x55};
constexpr hal::SpiWord kFileInfo{0x56};

constexpr hal::SpiWord kModeDownload{0x00FF};
constexpr hal::SpiWord kModeUpload{0x00AA};
constexpr hal::SpiWord kModeEnd{0x0000};

constexpr std::size_t kExtChars = 4;

constexpr std::uint8_t kEmptyCheatSet[2] = {0, 0};
constexpr IoIndex kCheatIndex{255};

constexpr std::uint8_t ascii_upper(std::uint8_t c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<std::uint8_t>(c - 0x20u) : c;
}

Ex<hal::SpiWord> beat(hal::ISpiTransport& link, std::uint16_t out) {
    return link.transfer(hal::SpiWord{out});
}

Ex<void> emit_payload(hal::ISpiTransport& link, std::span<const std::uint8_t> data, hal::Width w) {
    if (w == hal::Width::Word) {
        const std::size_t pairs = data.size() / 2u;
        for (std::size_t i = 0; i < pairs; ++i) {
            const auto lo = static_cast<std::uint16_t>(data[2u * i]);
            const auto hi = static_cast<std::uint16_t>(data[2u * i + 1u]);
            const auto word = static_cast<std::uint16_t>(lo | (hi << 8));
            if (auto r = beat(link, word); !r) return std::unexpected(r.error());
        }
        if ((data.size() & 1u) != 0u) {
            const auto tail = static_cast<std::uint16_t>(data.back());
            if (auto r = beat(link, tail); !r) return std::unexpected(r.error());
        }
        return {};
    }
    for (const std::uint8_t byte : data) {
        if (auto r = beat(link, byte); !r) return std::unexpected(r.error());
    }
    return {};
}

Ex<void> absorb_payload(hal::ISpiTransport& link, std::span<std::uint8_t> data, hal::Width w) {
    if (w == hal::Width::Word) {
        const std::size_t pairs = data.size() / 2u;
        for (std::size_t i = 0; i < pairs; ++i) {
            auto r = beat(link, 0);
            if (!r) return std::unexpected(r.error());
            data[2u * i] = static_cast<std::uint8_t>(r->v & 0x00FFu);
            data[2u * i + 1u] = static_cast<std::uint8_t>((r->v >> 8) & 0x00FFu);
        }
        if ((data.size() & 1u) != 0u) {
            auto r = beat(link, 0);
            if (!r) return std::unexpected(r.error());
            data.back() = static_cast<std::uint8_t>(r->v & 0x00FFu);
        }
        return {};
    }
    for (std::uint8_t& byte : data) {
        auto r = beat(link, 0);
        if (!r) return std::unexpected(r.error());
        byte = static_cast<std::uint8_t>(r->v & 0x00FFu);
    }
    return {};
}

Ex<void> write_index_register(hal::ISpiTransport& link, std::uint16_t index) {
    hal::Selected cs(link, hal::ChipSelect::Fpga);
    if (auto r = link.transfer(kFileIndex); !r) return std::unexpected(r.error());
    if (auto r = beat(link, index); !r) return std::unexpected(r.error());
    return {};
}

}  // namespace

Ex<void> DownloadSession::set_index(hal::ISpiTransport& link, IoIndex index) {
    return write_index_register(link, index.v);
}

Ex<void> DownloadSession::set_index(hal::ISpiTransport& link, WideIoIndex index) {
    return write_index_register(link, index.v);
}

Ex<void> DownloadSession::send_file_info(hal::ISpiTransport& link, std::string_view ext) {
    std::uint8_t c[kExtChars] = {0, 0, 0, 0};
    for (std::size_t i = 0; i < kExtChars && i < ext.size(); ++i) {
        const auto raw = static_cast<std::uint8_t>(ext[i]);
        if (raw == 0) break;
        c[i] = ascii_upper(raw);
    }
    const auto w0 = static_cast<std::uint16_t>((c[0] << 8) | c[1]);
    const auto w1 = static_cast<std::uint16_t>((c[2] << 8) | c[3]);

    hal::Selected cs(link, hal::ChipSelect::Fpga);
    if (auto r = link.transfer(kFileInfo); !r) return std::unexpected(r.error());
    if (auto r = beat(link, w0); !r) return std::unexpected(r.error());
    if (auto r = beat(link, w1); !r) return std::unexpected(r.error());
    return {};
}

Ex<DownloadSession> DownloadSession::begin(hal::ISpiTransport& link, IoIndex index) {
    return begin(link, WideIoIndex{index.v}, SessionParams{});
}

Ex<DownloadSession> DownloadSession::begin(hal::ISpiTransport& link, IoIndex index,
                                           const SessionParams& params) {
    return begin(link, WideIoIndex{index.v}, params);
}

Ex<DownloadSession> DownloadSession::begin(hal::ISpiTransport& link, WideIoIndex index,
                                           const SessionParams& params) {
    if (auto r = set_index(link, index); !r) return std::unexpected(r.error());
    if (!params.ext.empty()) {
        if (auto r = send_file_info(link, params.ext); !r) {
            return std::unexpected(r.error());
        }
    }

    std::optional<DownloadSession> session;
    {
        hal::Selected cs(link, hal::ChipSelect::Fpga);
        if (auto r = link.transfer(kFileTx); !r) return std::unexpected(r.error());
        const hal::SpiWord mode =
            (params.direction == TransferDirection::Upload) ? kModeUpload : kModeDownload;
        if (auto r = link.transfer(mode); !r) return std::unexpected(r.error());

        session.emplace(DownloadSession{link});
        session->index_ = index.v;
        session->direction_ = params.direction;

        if (params.aux.v != 0) {
            const auto lo = static_cast<std::uint16_t>(params.aux.v & 0xFFFFu);
            const auto hi = static_cast<std::uint16_t>((params.aux.v >> 16) & 0xFFFFu);
            auto r = beat(link, lo);
            if (r) r = beat(link, hi);
            if (!r) {
                cs.release();
                (void)session->end();
                return std::unexpected(r.error());
            }
        }
    }
    return std::move(*session);
}

Ex<DownloadSession> DownloadSession::open(hal::ISpiTransport& link, WideIoIndex index) {
    hal::Selected cs(link, hal::ChipSelect::Fpga);
    if (auto r = link.transfer(kFileTx); !r) return std::unexpected(r.error());
    if (auto r = link.transfer(kModeDownload); !r) return std::unexpected(r.error());
    DownloadSession session{link};
    session.index_ = index.v;
    return session;
}

DownloadSession::~DownloadSession() {
    if (posted_) {
        abandon();
    } else if (link_ != nullptr) {
        (void)end();
    }
}

DownloadSession::DownloadSession(DownloadSession&& o) noexcept
    : link_(std::exchange(o.link_, nullptr)), index_(o.index_), direction_(o.direction_),
      posted_(std::exchange(o.posted_, false)) {}

Ex<void> DownloadSession::write(std::span<const std::uint8_t> data) {
    if (link_ == nullptr) return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    return write(data, link_->width());
}

Ex<void> DownloadSession::write(std::span<const std::uint8_t> data, hal::Width w) {
    if (link_ == nullptr) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    if (direction_ != TransferDirection::Download) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), index_});
    }
    hal::Selected cs(*link_, hal::ChipSelect::Fpga);
    if (auto r = link_->transfer(kFileTxDat); !r) return std::unexpected(r.error());
    return emit_payload(*link_, data, w);
}

Ex<void> DownloadSession::read(std::span<std::uint8_t> data) {
    if (link_ == nullptr) return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    return read(data, link_->width());
}

Ex<void> DownloadSession::read(std::span<std::uint8_t> data, hal::Width w) {
    if (link_ == nullptr) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    if (direction_ != TransferDirection::Upload) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), index_});
    }
    hal::Selected cs(*link_, hal::ChipSelect::Fpga);
    if (auto r = link_->transfer(kFileTxDat); !r) return std::unexpected(r.error());
    return absorb_payload(*link_, data, w);
}

Ex<void> DownloadSession::post(std::span<const std::uint16_t> words) {
    if (link_ == nullptr || posted_ || words.empty() || direction_ != TransferDirection::Download) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), index_});
    }

    link_->select(hal::ChipSelect::Fpga);
    Ex<void> r = link_->transfer(kFileTxDat).transform([](hal::SpiWord) noexcept {});
    for (std::size_t i = 0; r && i + 1u < words.size(); ++i)
        r = beat(*link_, words[i]).transform([](hal::SpiWord) noexcept {});
    if (r) r = link_->post(hal::SpiWord{words.back()});
    if (!r) {
        link_->deselect();
        return r;
    }
    posted_ = true;
    return {};
}

Ex<bool> DownloadSession::settle(std::span<const std::uint16_t> tail) {
    if (!posted_) return std::unexpected(Error{Errc::negotiation, ERR_SITE(), index_});
    auto done = link_->posted_done();
    if (!done) {
        abandon();
        return std::unexpected(done.error());
    }
    if (!*done) return false;
    posted_ = false;
    Ex<void> r{};
    for (std::size_t i = 0; r && i < tail.size(); ++i)
        r = beat(*link_, tail[i]).transform([](hal::SpiWord) noexcept {});
    link_->deselect();
    if (!r) return std::unexpected(r.error());
    return true;
}

void DownloadSession::abandon() noexcept {
    if (!posted_) return;
    posted_ = false;
    link_->abandon_post();
    link_->deselect();
    link_ = nullptr;
}

Ex<void> DownloadSession::end() {
    if (link_ == nullptr) return {};
    if (posted_) return std::unexpected(Error{Errc::would_block, ERR_SITE(), index_});

    hal::ISpiTransport* link = std::exchange(link_, nullptr);
    hal::Selected cs(*link, hal::ChipSelect::Fpga);
    if (auto r = link->transfer(kFileTx); !r) return std::unexpected(r.error());
    if (auto r = link->transfer(kModeEnd); !r) return std::unexpected(r.error());
    return {};
}

Ex<void> DownloadSession::send_cheats(hal::ISpiTransport& link,
                                      std::span<const std::uint8_t> table) {
    auto session = begin(link, kCheatIndex);
    if (!session) return std::unexpected(session.error());

    const std::span<const std::uint8_t> payload =
        table.empty() ? std::span<const std::uint8_t>{kEmptyCheatSet} : table;
    if (auto r = session->write(payload); !r) {
        return r;
    }
    return session->end();
}

}  // namespace mister::proto
