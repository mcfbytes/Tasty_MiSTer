// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/servant_factories.h"

#include <memory>

#include "cores/registry.h"
#include "cores/servant_grant.h"
#include "infra/prelude.h"

namespace mister::cores {

ServantSet make_servants(const svc::Vfs& vfs, const os::IClock& clock) {
    const ServantGrant grant{.vfs = vfs, .clock = infra::OptRef<const os::IClock>{clock}};
    ServantSet set{};
    for (const CoreFactory& row : core_table()) {
        if (!row.has_mailbox_servant())
            continue;
        std::unique_ptr<IMailboxServant>& kept = set[row.mailbox_servant.id];
        if (kept == nullptr)
            kept = row.mailbox_servant.make(grant);
    }
    return set;
}

}
