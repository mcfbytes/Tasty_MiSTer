// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "app/path_text.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::app {

class FileBytes {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::uint16_t kRecycleSlots = 15;
    static constexpr std::uint16_t kConfigSlots = 10;
    static constexpr std::uint16_t kConfigBase = kRecycleSlots + 1;
    static constexpr proto::FileId kManifestId{kConfigBase + kConfigSlots};
    static constexpr std::uint16_t kSlots = kManifestId.v + 1;
    static constexpr std::uint16_t kNone = 0;

    static constexpr std::uint16_t kMaxLiveFiles = 14;

    static constexpr std::size_t kPieceBytes = 1024u * 1024u;

    static constexpr std::uint32_t kHold = 2;

    [[nodiscard]] static constexpr proto::FileId config_id(std::uint8_t slot) noexcept {
        return proto::FileId{static_cast<std::uint16_t>(kConfigBase + slot)};
    }

    struct Slot {
        std::vector<std::uint8_t> bytes;
        FixedStr<8, StrFit::Reject> ext{};
        PathText path{};
        std::uint32_t load_addr = 0;
        std::uint64_t size_bytes = 0;
        std::uint64_t offset = 0;
        std::uint32_t crc = 0;
        proto::FileId save{};
    };

    [[nodiscard]] Ex<proto::FileId> intern(std::vector<std::uint8_t>&& bytes, std::string_view ext,
                                           std::string_view path, std::uint32_t load_addr,
                                           std::uint32_t at, std::uint32_t popped,
                                           std::uint32_t crc = 0, std::uint64_t whole = 0,
                                           std::uint64_t offset = 0);

    [[nodiscard]] bool stamp_save(proto::FileId file, proto::FileId save) noexcept;

    [[nodiscard]] Ex<proto::FileId> intern_path(std::string_view path, std::uint64_t size_bytes,
                                                std::uint32_t at, std::uint32_t popped);

    [[nodiscard]] Ex<proto::FileId> intern_at(proto::FileId id, std::vector<std::uint8_t>&& bytes,
                                              std::string_view ext, std::string_view path,
                                              std::uint32_t load_addr);

    [[nodiscard]] const Slot* get(proto::FileId id) const noexcept;

private:
    [[nodiscard]] bool free_(std::uint16_t id, std::uint32_t popped) const noexcept {
        if (!live_[id]) return true;
        return static_cast<std::int32_t>(popped - named_at_[id]) >=
               static_cast<std::int32_t>(kHold);
    }

    void release_free_(std::uint32_t popped) noexcept;
    [[nodiscard]] std::uint16_t live_files_(std::uint32_t popped) const noexcept;

    Slot storage_[kSlots]{};
    std::uint32_t named_at_[kSlots]{};
    bool live_[kSlots]{};
    std::uint16_t next_ = 1;
};

}  // namespace mister::app
