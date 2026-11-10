// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/disc_backing.h"
#include "svc/file.h"
#include "svc/track.h"

namespace mister::svc {

class FileTableBacking final : public IDiscBacking {
    TASTY_SEAT_RESIDENT(Io);

public:
    [[nodiscard]] OpenFileCount count() const noexcept { return file_count_; }

    [[nodiscard]] bool holds(OpenFileId id) const noexcept { return file_count_.contains(id); }

    [[nodiscard]] Ex<OpenFileId> push(std::unique_ptr<IFile> f) noexcept {
        constexpr OpenFileCount capacity{kMaxTracks};
        const OpenFileId slot = file_count_.next_index();
        if (!capacity.contains(slot))
            return std::unexpected(capacity.refusal(Errc::bad_format, ERR_SITE()));
        files_[slot] = std::move(f);
        file_count_ += OpenFileCount{1};
        return slot;
    }

    void hold_single(std::unique_ptr<IFile> f) noexcept {
        files_[OpenFileId{0}] = std::move(f);
        file_count_ = OpenFileCount{1};
    }

    [[nodiscard]] IFile* file(OpenFileId i) const noexcept { return files_[i].get(); }

    void reset() noexcept {
        for (auto& f : files_)
            f.reset();
        file_count_ = OpenFileCount{};
    }

    void advise_first_window(const Track& t0) noexcept;

    void advise_ahead(proto::Lba lba, const Track& t, proto::BlockCount sectors) noexcept;

private:
    [[nodiscard]] Ex<void> do_read_payload(
        proto::Lba lba,
        const Track& t,
        std::uint32_t s_off,
        std::span<std::byte> out
    ) override;

    [[nodiscard]] IFile* resident_(OpenFileId id) const noexcept {
        return file_count_.contains(id) ? files_[id].get() : nullptr;
    }

    infra::IndexedArray<std::unique_ptr<IFile>, kMaxTracks, OpenFileId> files_{};
    OpenFileCount file_count_{};
};

}
