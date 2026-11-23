// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/c64/easyflash_image.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "infra/seat_owner_main.h"
#include "infra/span_cut.h"
#include "svc/save_frame_layer.h"

namespace mister::cores::c64 {

namespace {

constexpr std::size_t kHardwareHighAt = 0x16;
constexpr std::size_t kHardwareLowAt = 0x17;
constexpr std::uint8_t kEasyFlashType = 32;
constexpr std::uint8_t kEasyFlashXbankType = 33;

constexpr std::uint64_t kPacketsPerBank = 2;

[[nodiscard]] std::optional<proto::FileOffset> slot_of(const ChipPacket& packet) noexcept {
    const std::uint64_t bank = packet[detail::kChipBankAt];
    const bool romh = packet[detail::kChipPageAt] > detail::kRomlPage;
    const std::uint64_t index = bank * kPacketsPerBank + (romh ? 1 : 0);
    if (index >= kEasyFlashPackets)
        return std::nullopt;
    return proto::kFileStart + kCrtHeaderBytes + kChipPacketBytes * index;
}

}

bool EasyFlashImage::named_by(const CrtHeader& header) noexcept {
    const std::uint8_t type = header[kHardwareLowAt];
    return header[kHardwareHighAt] == 0 && (type == kEasyFlashType || type == kEasyFlashXbankType);
}

EasyFlashImage::EasyFlashImage(const CrtHeader& header)
    : bytes_(kEasyFlashImageBytes.file_word<std::size_t>(), kErased) {
    TASTY_SEAT_BODY(EasyFlashImage);
    const std::span<std::byte> image = std::as_writable_bytes(std::span(bytes_));
    const svc::SaveFrameLayerT<seat_owner::Main> frame{kEasyFlashBanks};
    frame.lay_lead(image, std::as_bytes(std::span(header)));
    frame.lay_heads(image, std::as_bytes(std::span(kEasyFlashHeads)));
}

bool EasyFlashImage::place(const ChipPacket& packet) noexcept {
    TASTY_SEAT_BODY(EasyFlashImage);
    const std::optional<proto::FileOffset> slot = slot_of(packet);
    if (!slot)
        return false;
    std::ranges::copy(packet, infra::affine::from(std::span(bytes_), *slot).begin());
    return true;
}

std::vector<std::uint8_t> EasyFlashImage::release() && noexcept {
    TASTY_SEAT_BODY(EasyFlashImage);
    return std::move(bytes_);
}

}
