// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_fio_queue.h"

#include <algorithm>
#include <cstring>

#include "hal/selected.h"

namespace mister::proto {

namespace {

[[nodiscard]] std::uint32_t beats(std::size_t bytes, hal::Width w) noexcept {
    return static_cast<std::uint32_t>(w == hal::Width::Word ? (bytes + 1u) / 2u : bytes);
}

}  // namespace

struct SpiFioQueue::Cost {
    TASTY_SEAT_RESIDENT(RT);
    using Result = std::uint32_t;
    hal::Width width;
    std::uint32_t on(const FioWindow::Index&) const noexcept { return 2; }
    std::uint32_t on(const FioWindow::Open&) const noexcept { return 2; }
    std::uint32_t on(const FioWindow::Data& d) const noexcept { return 1 + beats(d.len, width); }
    std::uint32_t on(const FioWindow::Close&) const noexcept { return 2; }
    std::uint32_t on(const FioWindow::IoCommand& c) const noexcept { return c.words; }
    std::uint32_t misrouted(const FioWindow&) const noexcept { return 0; }
};

Ex<void> SpiFioQueue::Emitter::on(const FioWindow::Index& i) noexcept {
    latched = i.index;
    return DownloadSession::set_index(*link, WideIoIndex{i.index});
}

Ex<void> SpiFioQueue::Emitter::on(const FioWindow::Open&) noexcept {
    auto s = DownloadSession::open(*link, WideIoIndex{latched});
    if (!s) return std::unexpected(s.error());
    open.emplace(std::move(*s));
    return {};
}

Ex<void> SpiFioQueue::Emitter::on(const FioWindow::Data& d) noexcept {
    if (!open) return std::unexpected(Error{Errc::negotiation, ERR_SITE(), d.offset});
    return open->write(std::span<const std::uint8_t>(slab + d.offset, d.len));
}

Ex<void> SpiFioQueue::Emitter::on(const FioWindow::Close&) noexcept {
    if (!open) return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    auto r = open->end();
    open.reset();
    return r;
}

Ex<void> SpiFioQueue::Emitter::on(const FioWindow::IoCommand& c) noexcept {
    hal::Selected cs(*link, hal::ChipSelect::Io);
    for (std::size_t i = 0; i < c.words; ++i) {
        const std::size_t at = c.offset + 2u * i;
        const auto w = static_cast<std::uint16_t>(slab[at] | (slab[at + 1] << 8));
        if (auto r = link->transfer(hal::SpiWord{w}); !r) return std::unexpected(r.error());
    }
    return {};
}

Ex<void> SpiFioQueue::Emitter::misrouted(const FioWindow&) noexcept {
    return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), 0});
}

SpiFioQueue::SpiFioQueue(hal::ISpiTransport& link, std::uint32_t piece_words) noexcept
    : link_(&link),
      piece_words_(std::clamp<std::uint32_t>(piece_words, 1u, tightest_round_budget_words() - 1u)) {
}

std::uint32_t SpiFioQueue::cost_(const FioWindow& w) const noexcept {
    Cost c{link_->width()};
    return infra::dispatch<infra::AllRouted>(w, c);
}

bool SpiFioQueue::fits(std::size_t windows, std::size_t bytes,
                       std::size_t brackets) const noexcept {
    TASTY_SEAT_BODY(SpiFioQueue);
    const bool restarts = head_ == size_;
    const std::size_t used_windows = restarts ? 0u : size_;
    const std::size_t used_bytes = restarts ? 0u : slab_used_;
    return brackets <= kMaxBrackets && used_bytes + bytes <= kSlabBytes &&
           used_windows + windows <= kWindows;
}

std::size_t SpiFioQueue::bracket_windows(std::size_t bytes) const noexcept {
    return windows_for(bytes,
                       std::size_t{piece_words_} * (link_->width() == hal::Width::Word ? 2u : 1u));
}

Ex<void> SpiFioQueue::submit(std::span<const Bracket> act) {
    TASTY_SEAT_BODY(SpiFioQueue);
    if (!idle()) {
        ++stats_.busy;
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }
    return place_(act);
}

Ex<void> SpiFioQueue::append(std::span<const Bracket> act) {
    TASTY_SEAT_BODY(SpiFioQueue);
    return place_(act);
}

Ex<void> SpiFioQueue::append_command(std::span<const std::uint16_t> words) {
    TASTY_SEAT_BODY(SpiFioQueue);
    restart_if_idle_();
    const std::size_t bytes = 2u * words.size();
    if (words.empty() || words.size() > kMaxCommandWords || size_ + 1u > kWindows ||
        slab_used_ + bytes > kSlabBytes) {
        ++stats_.oversize;
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(words.size())});
    }
    const std::size_t at = slab_used_;
    for (std::size_t i = 0; i < words.size(); ++i) {
        slab_[at + 2u * i] = static_cast<std::uint8_t>(words[i] & 0xFFu);
        slab_[at + 2u * i + 1u] = static_cast<std::uint8_t>(words[i] >> 8);
    }
    slab_used_ += bytes;
    q_[size_++] = infra::make<FioWindow>(FioWindow::IoCommand{
        static_cast<std::uint16_t>(at), static_cast<std::uint16_t>(words.size())});
    return {};
}

void SpiFioQueue::restart_if_idle_() noexcept {
    if (head_ != size_) return;
    head_ = 0;
    size_ = 0;
    slab_used_ = 0;
}

Ex<void> SpiFioQueue::place_(std::span<const Bracket> act) {
    restart_if_idle_();
    const std::size_t piece =
        std::size_t{piece_words_} * (link_->width() == hal::Width::Word ? 2u : 1u);
    std::size_t bytes = 0;
    std::size_t windows = 0;
    for (const Bracket& b : act) {
        bytes += b.bytes.size();
        windows += windows_for(b.bytes.size(), piece);
    }
    if (act.size() > kMaxBrackets || slab_used_ + bytes > kSlabBytes ||
        size_ + windows > kWindows) {
        ++stats_.oversize;
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(bytes)});
    }
    std::size_t at = slab_used_;
    for (const Bracket& b : act) {
        q_[size_++] = infra::make<FioWindow>(FioWindow::Index{b.index.v});
        q_[size_++] = infra::make<FioWindow>(FioWindow::Open{});
        std::size_t off = 0;
        do {
            const std::size_t len = std::min(piece, b.bytes.size() - off);
            if (len != 0) std::memcpy(slab_.data() + at, b.bytes.data() + off, len);
            q_[size_++] = infra::make<FioWindow>(
                FioWindow::Data{static_cast<std::uint16_t>(at), static_cast<std::uint16_t>(len)});
            at += len;
            off += len;
        } while (off < b.bytes.size());
        q_[size_++] = infra::make<FioWindow>(FioWindow::Close{});
    }
    slab_used_ = at;
    ++stats_.acts;
    return {};
}

std::uint32_t SpiFioQueue::drain(std::uint32_t budget_words) noexcept {
    TASTY_SEAT_BODY(SpiFioQueue);
    std::uint32_t used = 0;
    if (held_) return used;
    while (head_ != size_) {
        const FioWindow& w = q_[head_];
        const std::uint32_t c = cost_(w);
        if (c > budget_words - used) break;
        ++head_;
        const Ex<void> r = infra::dispatch<infra::AllRouted>(w, wire_);
        used += c;
        ++stats_.windows;
        stats_.words += c;
        if (!r) {
            ++stats_.wire_faults;
            abandon_();
            break;
        }
    }
    return used;
}

std::uint32_t SpiFioQueue::flush() noexcept {
    TASTY_SEAT_BODY(SpiFioQueue);
    if (idle()) return 0;
    ++stats_.flushes;
    return drain(kUnbudgeted);
}

void SpiFioQueue::abandon() noexcept {
    TASTY_SEAT_BODY(SpiFioQueue);
    abandon_();
}

void SpiFioQueue::abandon_() noexcept {
    if (head_ == size_ && !wire_.open) return;
    head_ = 0;
    size_ = 0;
    slab_used_ = 0;
    if (wire_.open) {
        if (auto r = wire_.open->end(); !r) ++stats_.wire_faults;
        wire_.open.reset();
    }
    ++stats_.abandons;
}

void SpiFioQueue::declare_resets(const ResetTerms& terms) noexcept { terms_ = terms; }

void SpiFioQueue::before_status(const StatusWord& sent, const StatusWord& next) noexcept {
    TASTY_SEAT_BODY(SpiFioQueue);
    if (terms_.status_trips(sent, next)) reset_abandon_();
}

void SpiFioQueue::before_buttons(std::uint16_t sent, std::uint16_t next) noexcept {
    TASTY_SEAT_BODY(SpiFioQueue);
    if (terms_.buttons_trip(sent, next)) reset_abandon_();
}

void SpiFioQueue::before_reset_line() noexcept {
    TASTY_SEAT_BODY(SpiFioQueue);
    reset_abandon_();
}

void SpiFioQueue::reset_abandon_() noexcept {
    if (head_ == size_ && !wire_.open) return;
    ++stats_.resets;
    abandon_();
}

}  // namespace mister::proto
