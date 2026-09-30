// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace mister::cores::snes {

inline constexpr std::uint32_t kLoRomHeader = 0x007fc0;
inline constexpr std::uint32_t kHiRomHeader = 0x00ffc0;
inline constexpr std::uint32_t kExHiRomHeader = 0x40ffc0;
inline constexpr std::uint32_t kNoHeader = 0;

[[nodiscard]] constexpr std::uint32_t copier_offset(std::uint64_t file_size) noexcept {
    return (file_size & 512u) ? 512u : 0u;
}

[[nodiscard]] std::uint32_t score_header(std::span<const std::uint8_t> image,
                                         std::uint32_t addr) noexcept;

[[nodiscard]] std::uint32_t find_header(std::span<const std::uint8_t> image) noexcept;

inline constexpr std::uint32_t kHeaderWindowBytes = 64;

[[nodiscard]] std::uint16_t window_reset_vector(std::span<const std::uint8_t> hdr) noexcept;

[[nodiscard]] constexpr std::uint32_t resetop_addr(std::uint32_t addr,
                                                   std::uint16_t resetvector) noexcept {
    return (addr & ~0x7fffu) | (resetvector & 0x7fffu);
}

[[nodiscard]] std::uint32_t score_window(std::span<const std::uint8_t> hdr, std::uint8_t resetop,
                                         std::uint32_t addr) noexcept;

[[nodiscard]] std::uint32_t pick_header(std::uint32_t score_lo, std::uint32_t score_hi,
                                        std::uint32_t score_ex) noexcept;

inline constexpr std::uint32_t kSignatureProbe = 0x7FC0;
inline constexpr std::string_view kBsxBiosMagic{"Satellaview BS-X     "};
inline constexpr std::string_view kSufamiMagic{"BANDAI SFC-ADX"};
inline constexpr std::string_view kSufamiBackupMagic{"SFC-ADX BACKUP"};
inline constexpr std::uint32_t kSufamiStride = 1024 * 1024;
inline constexpr std::uint32_t kSufamiBackupOffset = 0x10;

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

inline constexpr std::uint32_t kTypingWindowBack = 16;
inline constexpr std::uint32_t kTypingWindowBytes = 96;

struct TypingBytes {
    std::uint8_t size_field = 0;
    std::uint8_t rom_type = 0;
    std::uint8_t reserved = 0;
    std::uint8_t region = 0;
};

[[nodiscard]] TypingBytes type_cart(std::span<const std::uint8_t> window, std::uint32_t window_base,
                                    std::uint32_t addr, std::uint32_t payload,
                                    const CartSniff& sniff) noexcept;

inline constexpr std::uint32_t kHeaderBlockBytes = 512;

[[nodiscard]] std::array<std::uint8_t, kHeaderBlockBytes> header_block(
    std::uint32_t addr, std::uint32_t payload, const TypingBytes* typing) noexcept;

[[nodiscard]] std::uint32_t next_pow2(std::uint32_t v) noexcept;

[[nodiscard]] std::uint32_t snes_mirror(std::uint32_t addr, std::uint32_t size) noexcept;

[[nodiscard]] std::uint32_t mirrored_size(std::uint32_t payload) noexcept;

struct MirrorRun {
    std::uint32_t src = 0;
    std::uint32_t len = 0;
};
[[nodiscard]] MirrorRun mirror_run(std::uint32_t pos, std::uint32_t payload,
                                   std::uint32_t padded) noexcept;

}  // namespace mister::cores::snes
