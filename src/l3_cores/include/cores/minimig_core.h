// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/config_blob.h"
#include "cores/core_support.h"
#include "cores/host_file_server.h"
#include "cores/nvram_store.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::cores {

enum class KickstartLayout : std::uint8_t {
    Rom256K,
    Rom512K,
    Rom512KMirrored,
    Rom1M,
    Rom1MSplit,
    Ext512K,
    Unknown,
};

inline constexpr std::uint32_t kConfigBlobMaxSize = 7270;
inline constexpr std::uint32_t kConfigBlobOldSize = 3100;

struct Mm2PushRow {
    std::uint8_t opcode;
    std::uint16_t value_offset;
    std::uint8_t width_words;
    std::string_view why;
};

std::span<const Mm2PushRow> mm2_push_table();

struct [[gnu::packed]] HrtmonConfigBlock {
    std::uint32_t mon_size;
    std::uint8_t col0h;
    std::uint8_t col0l;
    std::uint8_t col1h;
    std::uint8_t col1l;
    std::uint8_t right;
    std::uint8_t keyboard;
    std::uint8_t key;
    std::uint8_t ide;
    std::uint8_t a1200;
    std::uint8_t aga;
    std::uint8_t insert;
    std::uint8_t delay;
    std::uint8_t lview;
    std::uint8_t cd32;
    std::uint8_t screenmode;
    std::uint8_t novbr;
    std::uint8_t entered;
    std::uint8_t hexmode;

    static constexpr std::size_t kWireBytes = 22u;
};

struct HrtmonMaxChip {
    std::uint32_t maxchip;
};

static_assert(offsetof(HrtmonConfigBlock, mon_size) == 0);
static_assert(offsetof(HrtmonConfigBlock, col0h) == 4);
static_assert(
    offsetof(HrtmonConfigBlock, hexmode) == 21,
    "D4: hexmode must be the 18th single-byte field after mon_size "
    "(file offset +41) — not part of a flat uint32_t[20]"
);

static_assert(
    HrtmonConfigBlock::kWireBytes == 22,
    "D4: the block is 22 wire bytes — 4 (mon_size) + 18 single-byte "
    "fields, hrtmon_base+20..+41 (minimig_config.cpp:303-341)"
);
static_assert(
    sizeof(HrtmonConfigBlock) == HrtmonConfigBlock::kWireBytes,
    "D4: sizeof must not include tail padding — a serializer that "
    "takes sizeof would push 24 bytes and write 2 into the HRTmon ROM "
    "image at hrtmon_base+42..+43, which the classic never touches"
);
static_assert(
    offsetof(HrtmonConfigBlock, hexmode) + 1u == HrtmonConfigBlock::kWireBytes,
    "D4: the last wire byte is hexmode at +41; kWireBytes must be one "
    "past it"
);
static_assert(
    alignof(HrtmonConfigBlock) == 1,
    "D4: the block is a wire image, not an addressable record — a "
    "non-1 alignment is what reintroduces the tail padding"
);

static_assert(
    sizeof(HrtmonMaxChip) == 4,
    "D4: maxchip is a lone detached u32 at +68, never adjacent to "
    "HrtmonConfigBlock's fields"
);

class MinimigCore : public Core {
    TASTY_SEAT_RESIDENT(RT);

public:
    MinimigCore(const CoreProfile& profile, const HostServices& host) : Core(profile, host) {}

    [[nodiscard]] Ex<void> apply_configuration(const ConfigBlob& blob);

    [[nodiscard]] Ex<void> akiko_service(hal::SpiWord status_word);

    hal::SpiWord cached_status() const noexcept { return status_cache_; }

    [[nodiscard]] Ex<void> a2065_drain();

    [[nodiscard]] Ex<void> mfm_write_track(
        std::uint8_t drive,
        std::uint8_t track,
        std::span<const std::byte> data
    );

    [[nodiscard]] Ex<void> enqueue_keycode(proto::KeyCode code_with_osd_bit);

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;
    [[nodiscard]] Ex<void> do_reset() override;
    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;
    [[nodiscard]] Ex<void> on_osd_closed() override;

    hal::SpiWord status_cache_{};
};

}
