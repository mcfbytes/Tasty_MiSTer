// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/conf_stamp.h"
#include "svc/wpa_conf_file.h"

namespace mister::svc {

class WpaConfStoreLoaded {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] const WpaConfFile& file() const noexcept { return file_; }

    [[nodiscard]] ConfStamp stamp() const noexcept { return stamp_; }

private:
    friend class WpaConfStore;

    WpaConfStoreLoaded(WpaConfFile file, ConfStamp stamp) : file_(std::move(file)), stamp_(stamp) {}

    WpaConfFile file_;
    ConfStamp stamp_;
};

enum class WpaConfStoreSaveResult : std::uint8_t {
    Saved,
    Stale,
};

class WpaConfStore {
    TASTY_SEAT_RESIDENT(Connect);

public:
    explicit WpaConfStore(std::string path) : path_(std::move(path)) {}

    [[nodiscard]] Ex<WpaConfStoreLoaded> load() const;

    [[nodiscard]] Ex<WpaConfStoreSaveResult> save(const WpaConfFile& file, ConfStamp stamp) const;

private:
    std::string path_;
};

}
