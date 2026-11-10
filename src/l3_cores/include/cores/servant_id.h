// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "cores/mailbox_servant.h"
#include "infra/indexed_array.h"
#include "svc/vfs.h"

namespace mister::cores {

struct ServantId {
    std::uint8_t v = 0;
    static const ServantId None;
    static const ServantId Msu1;
    static constexpr std::size_t kCount = 2;
    friend constexpr bool operator==(ServantId, ServantId) = default;
};

inline constexpr ServantId ServantId::None{0};
inline constexpr ServantId ServantId::Msu1{1};

using ServantSet = infra::
    IndexedArray<std::unique_ptr<IMailboxServant>, ServantId::kCount, ServantId>;

[[nodiscard]] ServantSet make_servants(const svc::Vfs& vfs);

}
