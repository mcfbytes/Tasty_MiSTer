// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "proto/types.h"
#include "svc/disc_identity.h"

namespace mister::svc {

struct DiscGeometry;
class IDiscReader;

class IDiscRules {
public:
    [[nodiscard]] virtual DiscIdentity identify(const DiscGeometry&, IDiscReader&) const noexcept {
        return {};
    }

    [[nodiscard]] virtual proto::BlockSize block_size() const noexcept = 0;

    virtual void fill(
        const DiscGeometry& disc,
        IDiscReader& reader,
        proto::Lba lba,
        std::span<std::uint8_t> out
    ) const noexcept = 0;

protected:
    IDiscRules() = default;
    IDiscRules(const IDiscRules&) = default;
    IDiscRules& operator=(const IDiscRules&) = default;
    ~IDiscRules() = default;
};

}
