// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/a2065_window.h"

#include <algorithm>

#include "cores/minimig/lance_span.h"

namespace mister::cores::minimig {

namespace {
using SlotBytes = std::array<std::byte, svc::NicSlotWord::kBytes>;

using WordBytes = std::array<std::byte, sizeof(LanceWord)>;
constexpr LanceSpan kNextByte{1};
constexpr LanceSpan kNextWord{sizeof(LanceWord)};

constexpr std::byte kValid{0x01};
constexpr svc::NicSlotWord kLineRaised{1};
}

LanceWord A2065Window::ram_word(LanceAddr a) const noexcept {
    const WordBytes b{ram_byte(a), ram_byte(a + kNextByte)};
    return LanceWord::of_be(b);
}

void A2065Window::put_ram_word(LanceAddr a, LanceWord w) noexcept {
    WordBytes b{};
    w.store_be(b);
    put_ram_byte(a, b.front());
    put_ram_byte(a + kNextByte, b.back());
}

LanceWordPair A2065Window::ram_pair(LanceAddr a) const noexcept {
    return LanceWordPair{.low = ram_word(a), .high = ram_word(a + kNextWord)};
}

void A2065Window::ram_read(LanceAddr a, std::span<std::byte> dst) const noexcept {
    for (std::byte& b : dst) {
        b = ram_byte(a);
        a += kNextByte;
    }
}

void A2065Window::ram_write(LanceAddr a, std::span<const std::byte> src) noexcept {
    for (const std::byte b : src) {
        put_ram_byte(a, b);
        a += kNextByte;
    }
}

A2065CommandSeq A2065Window::write_index() const noexcept {
    return A2065CommandSeq::of(page_.load_slot(kWriteIndex));
}

A2065Command A2065Window::command(A2065CommandSeq at) const noexcept {
    return A2065Command::of(page_.load_slot(kCommandRing + at.slot_in(kRingEntries, kSlotStride)));
}

void A2065Window::store_shadow(const Shadow& csrs) noexcept {
    SlotBytes b{};
    std::span<std::byte> at(b);
    for (const LanceWord w : csrs) {
        w.store_le(at.first<sizeof(LanceWord)>());
        at = at.subspan<sizeof(LanceWord)>();
    }
    page_.store_slot(kCsrShadow, svc::NicSlotWord::load(b));
}

void A2065Window::store_interrupt(bool raised) noexcept {
    page_.store_slot(kInterrupt, raised ? kLineRaised : svc::NicSlotWord{});
}

void A2065Window::store_station(const svc::MacAddress& m) noexcept {
    constexpr std::size_t kFromByte = 2;
    SlotBytes b{};
    b.front() = kValid;
    std::ranges::copy(
        std::span<const std::byte>(m.bytes).subspan<kFromByte>(),
        std::span<std::byte>(b).subspan<sizeof(std::uint32_t)>().begin()
    );
    page_.store_slot(kStation, svc::NicSlotWord::load(b));
}

}
