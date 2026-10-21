// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/disc_backing.h"
#include "svc/track.h"
#include "svc/vfs.h"

namespace mister::svc {

class FileTableBacking final : public IDiscBacking {
    TASTY_SEAT_RESIDENT(Io);

public:
    [[nodiscard]] std::uint16_t count() const noexcept { return file_count_; }

    [[nodiscard]] std::uint16_t push(std::unique_ptr<IFile> f) noexcept {
        files_[file_count_] = std::move(f);
        return file_count_++;
    }

    void hold_single(std::unique_ptr<IFile> f) noexcept {
        files_[0] = std::move(f);
        file_count_ = 1;
    }

    [[nodiscard]] IFile* file(std::uint16_t i) const noexcept { return files_[i].get(); }

    void reset() noexcept {
        for (auto& f : files_)
            f.reset();
        file_count_ = 0;
    }

    void advise_first_window(const Track& t0) noexcept;

    void advise_ahead(proto::Lba lba, const Track& t, std::uint32_t sectors) noexcept;

private:
    [[nodiscard]] Ex<void> do_read_payload(
        proto::Lba lba,
        const Track& t,
        std::uint32_t s_off,
        std::span<std::byte> out
    ) override;

    std::unique_ptr<IFile> files_[kMaxTracks]{};
    std::uint16_t file_count_ = 0;
};

}
