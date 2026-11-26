// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "hal/types.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/aux_value.h"
#include "proto/link/fio_open.h"
#include "proto/link_frame.h"
#include "proto/types.h"

namespace mister::proto::link {

class FioBracketWords {
public:
    static constexpr hal::LinkWord kModeDownload{0x00FF};
    static constexpr hal::LinkWord kModeUpload{0x00AA};
    static constexpr hal::LinkWord kModeEnd{0x0000};

    [[nodiscard]] static Ex<void> set_index(ILinkFrame& frame, FioIndexByte index);
    [[nodiscard]] static Ex<void> set_index(ILinkFrame& frame, WideIoIndex index);
    [[nodiscard]] static Ex<void> send_file_info(ILinkFrame& frame, std::string_view ext);

    [[nodiscard]] static Ex<void> emit_open(ILinkFrame& frame, const FioOpen& open);

    [[nodiscard]] static Ex<void> emit_data(
        ILinkFrame& frame,
        hal::Width w,
        std::span<const std::uint8_t> bytes
    );

    [[nodiscard]] static Ex<void> emit_end(ILinkFrame& frame);

    template <class F>
    [[nodiscard]] static Ex<void> for_window_words(hal::LinkWord mode, AuxValue aux, F&& f) {
        if (auto r = f(mode); !r)
            return r;
        if (aux.empty())
            return {};
        const hal::LinkHalves halves = aux.halves();
        if (auto r = f(halves.low); !r)
            return r;
        return f(halves.high);
    }
};

}
