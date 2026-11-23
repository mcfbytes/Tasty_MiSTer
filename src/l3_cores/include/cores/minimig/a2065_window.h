// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/minimig/a2065_command.h"
#include "cores/minimig/lance_addr.h"
#include "cores/minimig/lance_word.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/seat.h"
#include "svc/nic_mac.h"
#include "svc/nic_slot_word.h"
#include "svc/nic_window.h"

namespace mister::cores::minimig {

class A2065Window {
    TASTY_SEAT_RESIDENT(Nic);

public:
    static constexpr A2065CommandCount kRingEntries{256};

    static constexpr std::size_t kShadowCsrs = 4;
    using Shadow = std::array<LanceWord, kShadowCsrs>;

    static constexpr infra::ByteOffset kWriteIndex{0x8008};
    static constexpr infra::ByteOffset kCsrShadow{0x8010};
    static constexpr infra::ByteOffset kInterrupt{0x8018};
    static constexpr infra::ByteOffset kStation{0x8028};
    static constexpr infra::ByteOffset kCommandRing{0x8800};
    static constexpr infra::ByteCount kSlotStride{svc::NicSlotWord::kBytes};

    explicit A2065Window(svc::INicWindow& page) noexcept : page_(page) {}

    [[nodiscard]] std::byte ram_byte(LanceAddr a) const noexcept {
        return page_.load(a.lane_offset());
    }

    void put_ram_byte(LanceAddr a, std::byte b) noexcept { page_.store(a.lane_offset(), b); }

    [[nodiscard]] LanceWord ram_word(LanceAddr a) const noexcept;
    void put_ram_word(LanceAddr a, LanceWord w) noexcept;

    [[nodiscard]] LanceWordPair ram_pair(LanceAddr a) const noexcept;
    void ram_read(LanceAddr a, std::span<std::byte> dst) const noexcept;
    void ram_write(LanceAddr a, std::span<const std::byte> src) noexcept;

    [[nodiscard]] A2065CommandSeq write_index() const noexcept;

    [[nodiscard]] A2065Command command(A2065CommandSeq at) const noexcept;
    void store_shadow(const Shadow& csrs) noexcept;
    void store_interrupt(bool raised) noexcept;

    void store_station(const svc::MacAddress& m) noexcept;

private:
    svc::INicWindow& page_;
};

}
