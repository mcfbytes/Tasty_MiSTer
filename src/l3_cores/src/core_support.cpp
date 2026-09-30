// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/core_support.h"

#include <algorithm>

namespace mister::cores {

Ex<void> Core::init(proto::CoreSession& s) {
    if (inited_) {
        fatal(Error{Errc::core_load, ERR_SITE(), 0}, "Core::init twice");
    }
    if (auto r = pre_init_(s); !r) return r;
    if (auto r = do_init(s); !r) return r;
    inited_ = true;
    return {};
}

Ex<std::optional<proto::StatusBit>> Core::reset(const proto::ResetEdge& edge) {
    if (profile_->reset.reacts(edge)) {
        if (auto r = do_reset(); !r) return std::unexpected(r.error());
    }
    return profile_->reset.pulse(edge);
}

Ex<void> Core::shutdown() {
    auto flushed = flush_dirty_state_();
    auto down = do_shutdown();
    inited_ = false;
    if (!flushed) return flushed;
    return down;
}

Ex<void> Core::pre_init_(proto::CoreSession& s) {
    s.set_suppress_status_wire(profile_->suppress_reset_status_write);
    return {};
}

Ex<void> Core::flush_dirty_state_() { return {}; }

Ex<void> Core::mount(IoIndex slot, const MountedPath& p) {
    const bool declared = std::any_of(profile_->slots.begin(), profile_->slots.end(),
                                      [slot](const FileSlot& fs) { return fs.index == slot; });
    if (!declared && !profile_->slots.empty()) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot.v});
    }
    return on_mount(slot, p);
}

Ex<void> Core::file_tx(IoIndex index, svc::IFile& f) { return on_file_tx(index, f); }

Ex<void> Core::osd_closed() { return on_osd_closed(); }

}  // namespace mister::cores
