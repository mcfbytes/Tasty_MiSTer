// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "svc/types.h"
#include "app/file_intern.h"
#include "app/interned_file.h"
#include "app/ring_slab.h"
#include "app/types.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::app {

template <SeatTag Producer>
class FileBytes {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kPool = std::bit_ceil(std::size_t{InternedFile::kRecycleSlots});

    FileBytes() noexcept : slab_(InternedFile::kRecycleSlots) {}

    FileBytes(const FileBytes&) = delete;
    FileBytes& operator=(const FileBytes&) = delete;

    [[nodiscard]] Ex<proto::FileId> intern(FileIntern&& file);

    [[nodiscard]] bool stamp_remount(proto::FileId file) noexcept;

    [[nodiscard]] Ex<proto::FileId> intern_path(
        std::string_view path,
        proto::FileSize size,
        svc::Crc32 crc = {},
        std::uint32_t aux = 0
    );

    void send_staged(std::uint32_t at) noexcept { slab_.producer().send_staged(at); }

    [[nodiscard]] std::size_t retained_bytes() const noexcept;

    void release_before(std::uint32_t q) noexcept { slab_.consumer().release_before(q); }

    void take_sent() noexcept { slab_.consumer().take_sent(); }

    [[nodiscard]] infra::OptRef<const InternedFile> get(proto::FileId id) const noexcept;

private:
    using Slab = RingSlab<InternedFile, kPool, Producer, SeatTag::RT>;

    [[nodiscard]] static std::optional<proto::SlotIndex> pooled_slot_(proto::FileId id) noexcept;

    [[nodiscard]] infra::OptRef<InternedFile> staged_payload_(proto::FileId id) noexcept;

    [[nodiscard]] Ex<proto::SlotIndex> stage_(std::string_view path) noexcept;
    void reap_all_() noexcept;
    [[nodiscard]] std::uint16_t live_files_() const noexcept;

    Slab slab_;

    std::array<std::vector<std::uint8_t>, kPool> heap_{};
};

extern template class FileBytes<SeatTag::Main>;
extern template class FileBytes<SeatTag::Ui>;
extern template class FileBytes<SeatTag::Input>;

}
