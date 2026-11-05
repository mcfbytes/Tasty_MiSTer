// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "app/remembered_path.h"
#include "proto/conf_str.h"
#include "proto/types.h"
#include "ui/types.h"

namespace mister::cores {
struct FileSlot;
}

namespace mister::ui {

struct BuildEnv {
    proto::OsdMask hdmask{};
    proto::SubmenuNo page{};
    bool flat = false;
    bool cheats_available = false;
    bool manual_available = false;
    std::uint8_t dip_count = 0;
    RowCount osd_rows{16};

    std::string_view core_dir{};

    app::RememberedStem file_stem{};

    std::string_view cue_dir{};

    std::string_view custom_aspect_ratio[2]{};

    std::span<const cores::FileSlot> slots{};

    bool image_no_zip = false;

    proto::BindGeneration conf_gen{};
};

}
