// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

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

    [[nodiscard]] Ex<proto::FileId> intern(std::vector<std::uint8_t>&& bytes, std::string_view ext,
                                           std::string_view path, std::uint32_t load_addr,
                                           std::uint32_t crc = 0, std::uint64_t whole = 0,
                                           std::uint64_t offset = 0);

    [[nodiscard]] bool stamp_save(proto::FileId file, proto::FileId save) noexcept;

    [[nodiscard]] bool stamp_edge(proto::FileId file, ContentEdge edge) noexcept;

    [[nodiscard]] Ex<proto::FileId> intern_path(std::string_view path, std::uint64_t size_bytes,
                                                std::uint32_t crc = 0, std::uint32_t aux = 0);

    [[nodiscard]] Ex<proto::FileId> intern_at(proto::FileId id, std::vector<std::uint8_t>&& bytes,
                                              std::string_view ext, std::string_view path,
                                              std::uint32_t load_addr);

    void send_staged(std::uint32_t at) noexcept { slab_.producer().send_staged(at); }

    [[nodiscard]] std::size_t retained_bytes() const noexcept;

    void release_before(std::uint32_t q) noexcept { slab_.consumer().release_before(q); }
    void take_sent() noexcept { slab_.consumer().take_sent(); }

    [[nodiscard]] const InternedFile* get(proto::FileId id) const noexcept;

private:
    using Slab = RingSlab<InternedFile, kPool, Producer, SeatTag::RT>;

    [[nodiscard]] Ex<std::uint8_t> stage_(std::string_view path) noexcept;
    void reap_all_() noexcept;
    [[nodiscard]] std::uint16_t live_files_() const noexcept;

    Slab slab_;
    std::vector<std::uint8_t> heap_[kPool]{};
    InternedFile config_[InternedFile::kConfigSlots]{};
    std::vector<std::uint8_t> config_heap_[InternedFile::kConfigSlots]{};
};

extern template class FileBytes<SeatTag::Main>;
extern template class FileBytes<SeatTag::Ui>;
extern template class FileBytes<SeatTag::Input>;

}  // namespace mister::app
