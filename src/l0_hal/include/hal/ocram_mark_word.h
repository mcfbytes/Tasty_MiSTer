// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/ocram_types.h"
#include "hal/reg_field.h"

namespace mister::hal::ocram {

template <class Owner>
class RingWriter;

class MarkWord {
public:
    constexpr MarkWord() noexcept = default;

    [[nodiscard]] static constexpr MarkWord at(RingIndex index) noexcept {
        return MarkWord{kArmed.set(index.word(), RegField<std::uint32_t>::Value{1})};
    }

    [[nodiscard]] static constexpr MarkWord unarmed() noexcept { return MarkWord{}; }

    [[nodiscard]] constexpr bool armed() const noexcept { return kArmed.test(v); }

    [[nodiscard]] constexpr RingIndex index() const noexcept { return RingIndex::from_word(v); }

    friend constexpr bool operator==(MarkWord, MarkWord) = default;

private:
    template <class Owner>
    friend class RingWriter;
    static constexpr RegField<std::uint32_t> kArmed{31, 1};

    constexpr explicit MarkWord(std::uint32_t word) noexcept : v(word) {}

    [[nodiscard]] constexpr std::uint32_t word() const noexcept { return v; }

    std::uint32_t v = 0;
};

static_assert(
    MarkWord::at(RingIndex{0x1234}).armed()
        && MarkWord::at(RingIndex{0x1234}).index() == RingIndex{0x1234},
    "an armed mark keeps its index"
);
static_assert(!MarkWord::unarmed().armed(), "the unarmed word clears the cause");

}
