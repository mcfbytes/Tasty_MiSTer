// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "app/rec_control.h"
#include "proto/volume_cmd.h"

namespace mister::app {

struct CmdVerb {
    enum class RbfOrMra : std::uint8_t { Rbf, Mra };

    struct LoadCore {
        std::string_view path;
        RbfOrMra kind = RbfOrMra::Rbf;
    };
    struct Playlist {
        std::string_view path;
    };
    struct VideoMode {
        std::string_view spec;
    };
    struct FbCmd {
        std::string_view line;
    };
    struct Screenshot {
        std::string_view path;
        bool scaled = false;
    };
    struct Volume {
        proto::VolumeCmd cmd = proto::VolumeCmd::Relative;
        std::int8_t arg = 0;
    };
    struct RtStats {};

    struct TasPlay {
        std::string_view movie;
        std::string_view rom;
        std::optional<std::uint32_t> phase_us;
        std::optional<std::int32_t> lead;
    };
    struct TasStop {};

    struct RecStart {
        std::string_view path;
        RecMode mode = RecMode::Avi;
        RecOptions opt{};
    };
    struct RecArm {
        std::string_view path;
        RecMode mode = RecMode::Avi;
        RecOptions opt{};
    };
    struct RecStop {};
    struct RecDisarm {};
};

}  // namespace mister::app
