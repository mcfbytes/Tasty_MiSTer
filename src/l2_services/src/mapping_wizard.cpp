// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/mapping_wizard.h"

namespace mister::svc {

Ex<void> MappingWizard::begin(const DeviceIdentity& id, MapKind kind) {
    device_ = id;
    kind_ = kind;
    captured_ = std::nullopt;

    step_ = id.id.empty() ? Step::AwaitDevice : Step::AwaitButton;
    return {};
}

void MappingWizard::on_device(const DeviceIdentity& id) {
    if (step_ != Step::AwaitDevice)
        return;
    device_ = id;
    step_ = Step::AwaitButton;
}

void MappingWizard::on_button(std::uint16_t code) {
    if (step_ != Step::AwaitButton || code == 0)
        return;
    captured_ = code;
}

Ex<MappingWizard::Step> MappingWizard::advance() {

    switch (step_) {
        case Step::Idle:
            return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
        case Step::AwaitDevice:
            break;
        case Step::AwaitButton:
            if (captured_)
                step_ = Step::Confirm;
            break;
        case Step::Confirm:
            step_ = Step::Persist;
            break;
        case Step::Persist:

            step_ = Step::Done;
            break;
        case Step::Done:
        case Step::Cancelled:
            break;
    }
    return step_;
}

void MappingWizard::cancel() {
    step_ = Step::Cancelled;
}

}
