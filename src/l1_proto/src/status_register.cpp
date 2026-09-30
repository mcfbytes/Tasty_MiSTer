// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/status_register.h"
#include "hal/selected.h"

#include <charconv>
#include <string_view>

namespace mister::proto {

namespace {
constexpr hal::SpiWord kSetStatus2{0x1E};

constexpr unsigned kMaxFieldSpan = 8;

bool scan_decimal_prefix(std::string_view s, std::size_t& pos, unsigned& out) {
    std::uint32_t v = 0;
    const auto [ptr, ec] = std::from_chars(s.data() + pos, s.data() + s.size(), v, 10);
    if (ec != std::errc{} || ptr == s.data() + pos) return false;
    pos = static_cast<std::size_t>(ptr - s.data());
    out = v;
    return true;
}

bool index_char_value(char c, unsigned& out) {
    if (c >= '0' && c <= '9') {
        out = static_cast<unsigned>(c - '0');
        return true;
    }
    if (c >= 'A' && c <= 'V') {
        out = static_cast<unsigned>(c - 'A') + 10;
        return true;
    }
    return false;
}

bool decode_bit_ref(const char* spec, unsigned& start, unsigned& end) {
    if (spec == nullptr) return false;
    const std::string_view s(spec);
    if (s.empty()) return false;

    if (s.front() == '[') {

        std::size_t p = 1;
        unsigned a = 0;
        if (scan_decimal_prefix(s, p, a) && p < s.size() && s[p] == ':') {
            std::size_t p2 = p + 1;
            unsigned b = 0;
            if (scan_decimal_prefix(s, p2, b) && p2 < s.size() && s[p2] == ']') {
                end = a;
                start = b;
                if (start > 127 || end > 127 || end <= start) return false;
                return (end - start) <= kMaxFieldSpan;
            }
        }

        std::size_t p1 = 1;
        unsigned only = 0;
        if (!(scan_decimal_prefix(s, p1, only) && p1 < s.size() && s[p1] == ']')) {
            return false;
        }
        start = only;
        end = only;
        return only <= 127;
    }

    unsigned first = 0;
    if (!index_char_value(s[0], first)) return false;
    start = first;
    if (s.size() >= 2 && index_char_value(s[1], end)) {
        if (start > 127 || end > 127 || end <= start) return false;
    } else {
        end = start;
        if (start > 127) return false;
    }
    return (end - start) <= kMaxFieldSpan;
}
}  // namespace

void StatusRegister::set_bit(StatusBit bit, bool value) {
    if (bit.v >= kBits) {
        fatal(Error{Errc::slot_range, ERR_SITE(), bit.v}, "status bit");
    }
    const unsigned w = bit.v / 16u, b = bit.v % 16u;
    const auto mask = static_cast<std::uint16_t>(1u << b);
    const std::uint16_t next =
        value ? static_cast<std::uint16_t>(words_[w] | mask)
              : static_cast<std::uint16_t>(words_[w] & static_cast<std::uint16_t>(~mask));
    if (next != words_[w]) {
        words_[w] = next;
        dirty_ = true;
    }
}

void StatusRegister::adopt(std::span<const std::uint16_t, kWords> words) noexcept {
    for (unsigned i = 0; i < kWords; ++i)
        words_[i] = words[i];
    dirty_ = true;
}

StatusWord StatusRegister::value() const noexcept {
    StatusWord out{};
    static_assert(StatusWord::kWords == kWords);
    for (unsigned i = 0; i < kWords; ++i)
        out.words[i] = words_[i];
    return out;
}

bool StatusRegister::get_bit(StatusBit bit) const {
    if (bit.v >= kBits) {
        fatal(Error{Errc::slot_range, ERR_SITE(), bit.v}, "status bit");
    }
    return ((static_cast<unsigned>(words_[bit.v / 16u]) >> (bit.v % 16u)) & 1u) != 0u;
}

Ex<void> StatusRegister::set_field(const char* spec, std::uint32_t value) {
    unsigned start = 0, end = 0;
    if (!decode_bit_ref(spec, start, end)) {

        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 0});
    }
    const unsigned width = end - start + 1;
    for (unsigned i = 0; i < width; ++i) {
        const bool bit = ((value >> i) & 1u) != 0u;
        set_bit(StatusBit{static_cast<std::uint8_t>(start + i)}, bit);
    }
    return {};
}

Ex<void> StatusRegister::flush(hal::ISpiTransport& link) {
    const StatusWord next = value();
    if (fence_ != nullptr) fence_->before_status(sent_, next);
    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto r = link.transfer(kSetStatus2); !r) return std::unexpected(r.error());
    for (unsigned i = 0; i < kWords; ++i) {
        if (auto r = link.transfer(hal::SpiWord{words_[i]}); !r) {
            return std::unexpected(r.error());
        }
    }
    dirty_ = false;
    sent_ = next;
    return {};
}

}  // namespace mister::proto
