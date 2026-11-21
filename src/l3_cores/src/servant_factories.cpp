// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/servant_factories.h"

#include <memory>

#include "cores/registry.h"
#include "cores/servant_grant.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"

namespace mister::cores {

std::unique_ptr<IDiskServant> make_disk_servant(
    const svc::Vfs& vfs,
    infra::OptRef<xthread::WakeFlag> activity
) {
    const ServantGrant grant{.vfs = vfs, .activity = activity};
    for (const CoreFactory& row : core_table())
        if (row.serves_disks())
            return row.disk_servants.front().make(row.profile, grant);
    return nullptr;
}

ServantSet make_servants(const svc::Vfs& vfs) {
    const ServantGrant grant{.vfs = vfs};
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
