// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/device_facts.h"

namespace mister::svc {

void DeviceFacts::inherit_identity_from(const DeviceFacts& src) {
    id.vid = src.id.vid;
    id.pid = src.id.pid;
    id.version = src.id.version;
    id.bustype = src.id.bustype;
    id.source = src.id.source;
    quirk = src.quirk;
    id.name = src.id.name;
    id.id = src.id.id;
}

}
