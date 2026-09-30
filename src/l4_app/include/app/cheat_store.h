// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/cheat_blob_cell.h"
#include "app/cheat_catalog_cell.h"
#include "app/cheat_record.h"
#include "app/cheat_row.h"
#include "app/tx_digest_cell.h"
#include "cores/cheat_lookup.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class CheatStore {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit CheatStore(const svc::Vfs& vfs) noexcept : vfs_(&vfs) {}

    [[nodiscard]] Ex<void> open(const TxDigest& d, std::string_view core,
                                const cores::CheatLookup& look);

    void take(const CheatCatalog& cat);

    void close() noexcept;

    [[nodiscard]] Ex<bool> toggle(std::size_t i);

    [[nodiscard]] std::size_t count() const noexcept {
        TASTY_SEAT_BODY(CheatStore);
        return recs_.size();
    }

    [[nodiscard]] std::size_t loaded() const noexcept {
        TASTY_SEAT_BODY(CheatStore);
        return loaded_;
    }
    [[nodiscard]] std::uint32_t unit() const noexcept {
        TASTY_SEAT_BODY(CheatStore);
        return geom_.unit;
    }
    [[nodiscard]] CheatRow row(std::size_t i) const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> blob() const noexcept;
    [[nodiscard]] std::string_view archive() const noexcept {
        TASTY_SEAT_BODY(CheatStore);
        return archive_;
    }

    [[nodiscard]] const CheatBlobCell& blob_cell() const noexcept {
        TASTY_SEAT_BODY(CheatStore);
        return blob_cell_;
    }

    [[nodiscard]] std::uint32_t refusals() const noexcept {
        TASTY_SEAT_BODY(CheatStore);
        return refusals_;
    }
    [[nodiscard]] std::uint32_t opens() const noexcept {
        TASTY_SEAT_BODY(CheatStore);
        return opens_;
    }

private:
    void reset_() noexcept;
    void sort_() noexcept;
    void publish_() noexcept;

    const svc::Vfs* vfs_;
    std::vector<CheatRecord> recs_;
    cores::CheatGeometry geom_{};
    std::string archive_;
    CheatBlob blob_{};
    CheatBlobCell blob_cell_{};
    std::size_t loaded_ = 0;
    std::uint32_t refusals_ = 0;
    std::uint32_t opens_ = 0;
};

}  // namespace mister::app
