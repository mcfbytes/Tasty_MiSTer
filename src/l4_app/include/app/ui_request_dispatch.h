// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "app/ui_request.h"
#include "infra/message_sum.h"

namespace mister::app {

template <class S>
concept UiRequestSink = requires(S& s, const UiRequest& m, const UiRequest::Head& h) {
    s.on(std::declval<const UiRequest::LoadCore&>(), h);
    s.on(std::declval<const UiRequest::SaveConfig&>(), h);
    s.on(std::declval<const UiRequest::SaveDips&>(), h);
    s.on(std::declval<const UiRequest::SaveCoreConfig&>(), h);
    s.on(std::declval<const UiRequest::LoadCoreConfig&>(), h);
    s.on(std::declval<const UiRequest::LoadFile&>(), h);
    s.on(std::declval<const UiRequest::MountImage&>(), h);
    s.on(std::declval<const UiRequest::UnmountImage&>(), h);
    s.on(std::declval<const UiRequest::ResetCore&>(), h);
    s.on(std::declval<const UiRequest::Reboot&>(), h);
    s.on(std::declval<const UiRequest::LoadFileByDigit&>(), h);
    s.on(std::declval<const UiRequest::LoadRamImage&>(), h);
    s.misrouted(m);
};

using UiRequestRoutes = infra::AllRouted;

}  // namespace mister::app
