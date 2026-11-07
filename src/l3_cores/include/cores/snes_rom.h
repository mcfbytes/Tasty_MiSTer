// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/snes_header_fields.h"
#include "cores/types.h"
#include "infra/endian.h"
#include "proto/types.h"

namespace mister::cores::snes {

inline constexpr HeaderAddr kLoRomHeader{0x007fc0};
inline constexpr HeaderAddr kHiRomHeader{0x00ffc0};
inline constexpr HeaderAddr kExHiRomHeader{0x40ffc0};
inline constexpr HeaderAddr kNoHeader{};

inline constexpr HeaderAddr kImageStart{};

inline constexpr HeaderScore kRefused{};

inline constexpr std::uint32_t kCopierHeaderBytes = 512;

[[nodiscard]] constexpr std::uint32_t copier_offset(proto::FileSize file_size) noexcept {
    return (file_size.v & kCopierHeaderBytes) ? kCopierHeaderBytes : 0u;
}

inline constexpr ResetVector kResetVectorFloor{0x8000};

[[nodiscard]] HeaderScore score_header(
    std::span<const std::uint8_t> image,
    HeaderAddr addr
) noexcept;

[[nodiscard]] HeaderAddr find_header(std::span<const std::uint8_t> image) noexcept;

inline constexpr HeaderLen kHeaderWindow{64};

inline constexpr HeaderLen kOneByte{1};

inline constexpr HeaderAddr kSignatureProbe{0x7FC0};
inline constexpr std::string_view kBsxBiosMagic{"Satellaview BS-X     "};
inline constexpr std::string_view kSufamiMagic{"BANDAI SFC-ADX"};
inline constexpr std::string_view kSufamiBackupMagic{"SFC-ADX BACKUP"};
inline constexpr HeaderLen kSufamiStride{1024 * 1024};
inline constexpr HeaderLen kSufamiBackupOffset{0x10};

extern const std::array<std::uint8_t, 32> kCc92Signature;
extern const std::array<std::uint8_t, 32> kPf94TenKSignature;
extern const std::array<std::uint8_t, 64> kPf94OneMSignature;

struct CartSniff {
    bool bsx_bios = false;
    bool sufami_bios = false;
    bool sufami_base = false;
    bool sufami_turbo = false;
    bool cc92 = false;
    bool pf94 = false;
};

struct SufamiScan {
    bool bios = false;
    bool base = false;
    bool turbo = false;
    HeaderAddr addr{};
    friend constexpr bool operator==(SufamiScan, SufamiScan) = default;
};

[[nodiscard]] SufamiScan sufami_walk(std::span<const std::uint8_t> payload) noexcept;

inline constexpr HeaderLen kTypingWindowBack{16};
inline constexpr HeaderLen kTypingWindow{96};

struct ImageWindow {
    std::span<const std::uint8_t> bytes;
    HeaderAddr base;

    [[nodiscard]] std::uint8_t after(HeaderAddr addr, HeaderLen ahead) const noexcept;

    [[nodiscard]] std::uint8_t before(HeaderAddr addr, HeaderLen back) const noexcept;
};

[[nodiscard]] ImageWindow typing_window(
    std::span<const std::uint8_t> image,
    HeaderAddr addr
) noexcept;

struct TypingBytes {
    std::uint8_t size_field = 0;
    std::uint8_t rom_type = 0;
    std::uint8_t reserved = 0;
    std::uint8_t region = 0;
};

[[nodiscard]] TypingBytes type_cart(
    const ImageWindow& window,
    HeaderAddr addr,
    std::uint32_t payload,
    const CartSniff& sniff
) noexcept;

inline constexpr std::uint32_t kHeaderBlockBytes = 512;

[[nodiscard]] std::array<std::uint8_t, kHeaderBlockBytes> header_block(
    HeaderAddr addr,
    std::uint32_t payload,
    const TypingBytes* typing
) noexcept;

[[nodiscard]] std::uint32_t next_pow2(std::uint32_t v) noexcept;

[[nodiscard]] std::uint32_t snes_mirror(std::uint32_t addr, std::uint32_t size) noexcept;

[[nodiscard]] std::uint32_t mirrored_size(std::uint32_t payload) noexcept;

struct MirrorRun {
    std::uint32_t src = 0;
    std::uint32_t len = 0;
};

[[nodiscard]] MirrorRun mirror_run(
    std::uint32_t pos,
    std::uint32_t payload,
    std::uint32_t padded
) noexcept;

}
