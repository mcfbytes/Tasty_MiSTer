// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/disc_identity.h"

namespace mister::svc {

struct DiscGeometry;
class IDiscReader;

class IDiscIdentity {
public:
    [[nodiscard]] virtual DiscIdentity identify(
        const DiscGeometry& disc,
        IDiscReader& reader
    ) const noexcept = 0;

protected:
    IDiscIdentity() = default;
    IDiscIdentity(const IDiscIdentity&) = default;
    IDiscIdentity& operator=(const IDiscIdentity&) = default;
    ~IDiscIdentity() = default;
};

}
