// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/asset_query.h"

namespace mister::svc {

class GameAssetFinder {
    TASTY_SEAT_EXEMPT(component);

public:
    explicit GameAssetFinder(std::string_view root_path) noexcept : root_path_(root_path) {}

    [[nodiscard]] Ex<std::string> find(const AssetQuery& q) const;

private:
    std::string_view root_path_;
};

}
