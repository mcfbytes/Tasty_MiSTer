// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_fio_queue.h"
#include "proto/spi_frame.h"

#include <algorithm>
#include <cstring>
#include <span>

#include "hal/selected.h"
#include "infra/endian.h"

namespace mister::proto {

namespace {

constexpr std::size_t kPairBytes = 2;
}

Ex<void> SpiFioQueue::Emitter::on(const FioWindow::Index& i) noexcept {
    latched = i.index;
    SpiFrame frame{link};
    return DownloadSession::set_index(frame, latched);
}

Ex<void> SpiFioQueue::Emitter::on(const FioWindow::Open&) noexcept {
    auto s = DownloadSession::open(link, latched);
    if (!s)
        return std::unexpected(s.error());
    open.emplace(std::move(*s));
    return {};
}

Ex<void> SpiFioQueue::Emitter::on(const FioWindow::Data& d) noexcept {
    if (!open)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), d.offset});
    return open->write(slab.subspan(d.offset, d.len));
}

Ex<void> SpiFioQueue::Emitter::on(const FioWindow::Close&) noexcept {
    if (!open)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    auto r = open->end();
    open.reset();
    return r;
}

Ex<void> SpiFioQueue::Emitter::on(const FioWindow::IoCommand& c) noexcept {
    hal::Selected cs(link, hal::ChipSelect::Io);
    for (std::size_t i = 0; i < c.words; ++i) {
        const std::size_t at = c.offset + 2u * i;
        const auto w = infra::load_le<std::uint16_t>(slab.subspan(at).first<kPairBytes>());
        if (auto r = link.transfer(hal::SpiWord{w}); !r)
            return std::unexpected(r.error());
    }
    return {};
}

Ex<void> SpiFioQueue::Emitter::misrouted(const FioWindow&) noexcept {
    TASTY_SEAT_BODY(SpiFioQueue::Emitter);
    return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), 0});
}

SpiFioQueue::SpiFioQueue(
    hal::ISpiTransport& link,
    std::uint32_t piece_words,
    infra::OptRef<IFioTenant> tenant
) noexcept
    : link_(link),
      piece_words_(std::clamp<std::uint32_t>(piece_words, 1u, tightest_round_budget_words() - 1u)),
      tenant_(tenant) {}

std::uint32_t SpiFioQueue::cost_(const FioWindow& w) const noexcept {
    Cost c{link_.width()};
    return infra::dispatch<infra::AllRouted>(w, c);
}

bool SpiFioQueue::fits(ActShape act) const noexcept {
    TASTY_SEAT_BODY(SpiFioQueue);
    const bool restarts = head_ == size_;
    const std::size_t used_windows = restarts ? 0u : size_;
    const std::size_t used_bytes = restarts ? 0u : slab_used_;
    return act.brackets.within(BracketCount{kMaxBrackets})
        && act.bytes.within(FileSize{}, FileSize{kSlabBytes - used_bytes})
        && act.windows.fits_within(WindowCount{used_windows}, WindowCount{kWindows});
}

std::size_t SpiFioQueue::piece_bytes() const noexcept {
    return std::size_t{piece_words_} * (link_.width() == hal::Width::Word ? 2u : 1u);
}

SpiFioQueue::ActShape SpiFioQueue::shape_of(std::span<const Bracket> act) const noexcept {
    std::size_t bytes = 0;
    std::size_t windows = 0;
    for (const Bracket& b : act) {
        bytes += b.bytes.size();
        windows += bracket_windows(b.bytes.size());
    }
    return ActShape{
        .windows = WindowCount{windows},
        .bytes = FileSize{bytes},
        .brackets = BracketCount{act.size()}
    };
}

std::size_t SpiFioQueue::bracket_windows(std::size_t bytes) const noexcept {
    return windows_for(bytes, piece_bytes());
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
    if (words.empty()
        || words.size() > kMaxCommandWords
        || size_ + 1u > kWindows
        || slab_used_ + bytes > kSlabBytes) {
        ++stats_.oversize;
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(words.size())}
        );
    }
    const std::size_t at = slab_used_;
    for (std::size_t i = 0; i < words.size(); ++i) {
        slab_[at + 2u * i] = static_cast<std::uint8_t>(words[i]);
        slab_[at + 2u * i + 1u] = static_cast<std::uint8_t>(words[i] >> 8);
    }
    slab_used_ += bytes;
    q_[size_++] = infra::make<FioWindow>(FioWindow::IoCommand{
        static_cast<std::uint16_t>(at),
        static_cast<std::uint16_t>(words.size())
    });
    return {};
}

void SpiFioQueue::restart_if_idle_() noexcept {
    if (head_ != size_)
        return;
    head_ = 0;
    size_ = 0;
    slab_used_ = 0;
}

Ex<void> SpiFioQueue::place_(std::span<const Bracket> act) {
    restart_if_idle_();
    const ActShape shape = shape_of(act);
    if (!fits(shape)) {
        ++stats_.oversize;
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), shape.bytes.detail()});
    }
    const std::size_t piece = piece_bytes();
    std::size_t at = slab_used_;
    for (const Bracket& b : act) {
        q_[size_++] = infra::make<FioWindow>(FioWindow::Index{b.index});
        q_[size_++] = infra::make<FioWindow>(FioWindow::Open{});
        std::size_t off = 0;
        do {
            const std::size_t len = std::min(piece, b.bytes.size() - off);
            if (len != 0)
                std::memcpy(slab_.data() + at, b.bytes.data() + off, len);
            q_[size_++] = infra::make<FioWindow>(
                FioWindow::Data{static_cast<std::uint16_t>(at), static_cast<std::uint16_t>(len)}
            );
            at += len;
            off += len;
        } while (off < b.bytes.size());
        q_[size_++] = infra::make<FioWindow>(FioWindow::Close{});
    }
    slab_used_ = at;
    ++stats_.acts;
    return {};
}

FioWordCount SpiFioQueue::drain(FioWordCount budget_words) noexcept {
    TASTY_SEAT_BODY(SpiFioQueue);
    std::uint32_t used = 0;
    if (held_ || (tenant_ && tenant_->holds_fio()))
        return FioWordCount{used};
    while (head_ != size_) {
        const FioWindow& w = q_[head_];
        const std::uint32_t c = cost_(w);

        const FioWordCount left = budget_words.saturating_sub(FioWordCount{used});
        if (FioWordCount{c}.saturating_sub(left) != FioWordCount{})
            break;
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
    return FioWordCount{used};
}

FioWordCount SpiFioQueue::flush() noexcept {
    TASTY_SEAT_BODY(SpiFioQueue);
    if (idle())
        return FioWordCount{};
    ++stats_.flushes;
    return drain(FioWordCount{kUnbudgeted});
}

void SpiFioQueue::abandon() noexcept {
    TASTY_SEAT_BODY(SpiFioQueue);
    if (tenant_)
        tenant_->yield_to_reset();
    abandon_();
}

void SpiFioQueue::abandon_() noexcept {
    if (head_ == size_ && !wire_.open)
        return;
    head_ = 0;
    size_ = 0;
    slab_used_ = 0;
    if (wire_.open) {
        if (auto r = wire_.open->end(); !r)
            ++stats_.wire_faults;
        wire_.open.reset();
    }
    ++stats_.abandons;
}

void SpiFioQueue::declare_resets(const ResetTerms& terms) noexcept {
    terms_ = terms;
}

void SpiFioQueue::before_status(const StatusWord& sent, const StatusWord& next) noexcept {
    TASTY_SEAT_BODY(SpiFioQueue);
    if (terms_.status_trips(sent, next))
        reset_abandon_();
}

void SpiFioQueue::before_buttons(std::uint16_t sent, std::uint16_t next) noexcept {
    TASTY_SEAT_BODY(SpiFioQueue);
    if (terms_.buttons_trip(sent, next))
        reset_abandon_();
}

void SpiFioQueue::before_reset_line() noexcept {
    TASTY_SEAT_BODY(SpiFioQueue);
    reset_abandon_();
}

void SpiFioQueue::reset_abandon_() noexcept {
    if (tenant_)
        tenant_->yield_to_reset();
    if (head_ == size_ && !wire_.open)
        return;
    ++stats_.resets;
    abandon_();
}

}
