// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/disk_servant.h"

#include "svc/vfs.h"

#include "cores/acsi_servant.h"
#include "cores/manifests/st.h"
#include "infra/opt_ref.h"

namespace mister::cores {

namespace {

constexpr const char* kInquirySerial = TASTY_VDATE "  ";

}

std::unique_ptr<IDiskServant> make_disk_servant(const svc::Vfs& vfs) {
    return std::make_unique<AcsiServant>(
        infra::OptRef<const svc::Vfs>{vfs},
        manifests::kAtariStDiskFormats,
        kInquirySerial
    );
}

}
