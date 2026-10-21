// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/avi_write_status.h"
#include "app/encode_status.h"
#include "app/rec_write_status.h"
#include "app/recorder_status.h"
#include "infra/json_out.h"
#include "record_ctx.h"

namespace mister::fw {

struct RecView {
    app::RecorderStatus capture{};
    app::EncodeStatus encode{};
    app::RecWriteStatus write{};
    app::AviWriteStatus avi{};
};

constexpr void to_json(infra::JsonOut& o, const RecView& v) noexcept {
    to_json(o, v.capture);
    to_json(o, v.encode);
    to_json(o, v.write);
    to_json(o, v.encode.video);
    to_json(o, v.avi);
}

struct RecRecord {
    static constexpr infra::JsonName kKey{"rec"};
    using View = RecView;
    const app::RecorderStatusCell& capture;
    const app::EncodeStatusCell& encode;
    const app::RecWriteStatusCell& write;
    const app::AviWriteStatusCell& avi;

    [[nodiscard]] std::optional<View> sample(const RecordCtx&) const noexcept {
        View v;
        if (capture.sample_into(v.capture) == 0)
            return std::nullopt;
        (void)encode.sample_into(v.encode);
        (void)write.sample_into(v.write);
        (void)avi.sample_into(v.avi);
        return v;
    }
};

}
