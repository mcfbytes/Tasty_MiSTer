// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/boot_arm.h"

#include <cstdint>

#include "app/session_machine.h"
#include "app/fenced_core_signals.h"
#include "app/xml_kind.h"

namespace mister::app {

bool BootArm::in_boot_state() const noexcept {
    TASTY_SEAT_BODY(BootArm);
    return machine_.state() == SessionState::Boot;
}

Ex<LoadRequest> BootArm::argv_request(const BootArgs& args) const {
    TASTY_SEAT_BODY(BootArm);

    LoadRequest req{};
    if (args.core[0] != '\0') {
        if (!req.path.assign(args.core))
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});
        req.kind = xml_kind_of(args.core);
    }
    if (args.companion) {
        if (!req.xml.assign(*args.companion))
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 2});
    }

    if (req.kind != XmlKind::Rbf) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(req.kind)}
        );
    }
    return req;
}

UiRequest::LoadCore BootArm::program(const LoadRequest& req) {
    TASTY_SEAT_BODY(BootArm);
    UiRequest::LoadCore sw{};
    sw.path = req.path;
    sw.xml = req.kind;

    signals_.set_core_reset(true);
    return sw;
}

void BootArm::hold_core() {
    TASTY_SEAT_BODY(BootArm);
    signals_.set_core_reset(true);
}

}
