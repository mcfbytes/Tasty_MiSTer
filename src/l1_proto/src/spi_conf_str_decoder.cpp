// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_conf_str_decoder.h"

#include <span>

#include "hal/selected.h"

namespace mister::proto {

namespace {
constexpr hal::SpiWord kGetString{0x14};
}

static_assert(CoreSession::kConfStrCap <= 0xFFFFu, "ConfStr.bytes is 16 bits");

Ex<std::size_t> SpiConfStrDecoder::read_() noexcept {
    std::size_t n = 0;
    hal::Selected cs(*link_, hal::ChipSelect::Io);
    if (auto r = link_->transfer(kGetString); !r) return std::unexpected(r.error());
    while (n < buf_.size()) {
        auto r = link_->transfer(hal::SpiWord{0});
        if (!r) return std::unexpected(r.error());
        const auto byte = static_cast<std::uint8_t>(r->v & 0xFFu);
        if (byte == 0) break;
        buf_[n++] = byte;
    }
    return n;
}

void SpiConfStrDecoder::service() noexcept {
    if (!active()) return;
    const auto n = read_();
    if (!n) {
        (void)session_->accept_conf_str(std::unexpected(n.error()));
        refuse_(n.error().code);
        return;
    }
    ++reads_;

    if (const auto a = session_->accept_conf_str(std::span<const std::uint8_t>(buf_.data(), *n));
        !a) {
        refuse_(a.error().code);
        return;
    }
    publish_(*n);
}

void SpiConfStrDecoder::refuse_(Errc code) noexcept {
    if (!out_.push(LinkEvent::ConfStr{
            .read = LinkEvent::ConfStrRead::Refused, .bind_gen = *gen_, .err = code}))
        ++lost_;
}

void SpiConfStrDecoder::publish_(std::size_t n) noexcept {
    if (!push_body_(n)) ++lost_;
}

bool SpiConfStrDecoder::push_body_(std::size_t n) noexcept {
    constexpr std::size_t kChunk = ILinkRouter::kChunkBytes;
    const std::uint16_t chunks = n == 0 ? 0 : (n <= kChunk ? 1 : 2);
    const std::uint8_t* p = buf_.data();

    LinkEvent::ConfStr cs{.chunks = static_cast<std::uint8_t>(chunks),
                          .read = n == buf_.size() ? LinkEvent::ConfStrRead::Truncated
                                                   : LinkEvent::ConfStrRead::Whole,
                          .bind_gen = *gen_,
                          .bytes = static_cast<std::uint16_t>(n)};

    if (chunks == 0) return out_.push(cs);
    const std::size_t c0 = n <= kChunk ? n : kChunk;

    if (!out_.can_intern<LinkEvent::ConfStr>(c0, chunks)) {
        ++intern_refusals_;
        return false;
    }
    const auto id0 = out_.intern<LinkEvent::ConfStr>(std::span<const std::uint8_t>(p, c0));
    if (!id0) return false;
    cs.chunk0 = *id0;
    if (chunks == 2) {
        const auto id1 =
            out_.intern<LinkEvent::ConfStr>(std::span<const std::uint8_t>(p + c0, n - c0));
        if (!id1) return false;
        cs.chunk1 = *id1;
    }
    return out_.push(cs);
}

}  // namespace mister::proto
