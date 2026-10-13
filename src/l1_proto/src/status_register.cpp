// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/status_register.h"
#include "hal/selected.h"

#include <charconv>
#include <optional>
#include <string_view>

namespace mister::proto {

namespace {
constexpr hal::SpiWord kSetStatus2{0x1E};

constexpr unsigned kMaxFieldSpan = 8;

struct Digits {
    unsigned value;
    std::size_t pos;
};

[[nodiscard]] std::optional<Digits> scan_decimal_prefix(std::string_view s, std::size_t pos) {
    std::uint32_t v = 0;
    const auto [ptr, ec] = std::from_chars(s.data() + pos, s.data() + s.size(), v, 10);
    if (ec != std::errc{} || ptr == s.data() + pos) return std::nullopt;
    return Digits{static_cast<unsigned>(v), static_cast<std::size_t>(ptr - s.data())};
}

[[nodiscard]] std::optional<unsigned> index_char_value(char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
    if (c >= 'A' && c <= 'V') return static_cast<unsigned>(c - 'A') + 10;
    return std::nullopt;
}

struct BitRef {
    unsigned start;
    unsigned end;
};

[[nodiscard]] std::optional<BitRef> decode_bit_ref(const char* spec) {
    if (spec == nullptr) return std::nullopt;
    const std::string_view s(spec);
    if (s.empty()) return std::nullopt;

    if (s.front() == '[') {

        const auto a = scan_decimal_prefix(s, 1);
        if (a && a->pos < s.size() && s[a->pos] == ':') {
            const auto b = scan_decimal_prefix(s, a->pos + 1);
            if (b && b->pos < s.size() && s[b->pos] == ']') {
                const unsigned end = a->value;
                const unsigned start = b->value;
                if (start > 127 || end > 127 || end <= start) return std::nullopt;
                if ((end - start) > kMaxFieldSpan) return std::nullopt;
                return BitRef{start, end};
            }
        }

        const auto only = scan_decimal_prefix(s, 1);
        if (!only || only->pos >= s.size() || s[only->pos] != ']') return std::nullopt;
        if (only->value > 127) return std::nullopt;
        return BitRef{only->value, only->value};
    }

    const auto first = index_char_value(s[0]);
    if (!first) return std::nullopt;
    const unsigned start = *first;
    unsigned end = start;
    const auto second = s.size() >= 2 ? index_char_value(s[1]) : std::optional<unsigned>{};
    if (second) {
        end = *second;
        if (start > 127 || end > 127 || end <= start) return std::nullopt;
    } else {
        end = start;
        if (start > 127) return std::nullopt;
    }
    if ((end - start) > kMaxFieldSpan) return std::nullopt;
    return BitRef{start, end};
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
    const auto ref = decode_bit_ref(spec);
    if (!ref) {

        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 0});
    }
    const unsigned width = ref->end - ref->start + 1;
    for (unsigned i = 0; i < width; ++i) {
        const bool bit = ((value >> i) & 1u) != 0u;
        set_bit(StatusBit{static_cast<std::uint8_t>(ref->start + i)}, bit);
    }
    return {};
}

Ex<void> StatusRegister::flush(hal::ISpiTransport& link) {
    const StatusWord next = value();
    if (fence_) fence_->before_status(sent_, next);
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
