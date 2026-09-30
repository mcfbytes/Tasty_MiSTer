// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "proto/image_sink.h"
#include "svc/vfs.h"

namespace mister::cores {

inline constexpr std::size_t kFileTxChunkBytes = 4096;

struct FileTxCounters {
    std::uint32_t chunks = 0;
    bool opened = false;
};

[[nodiscard]] Ex<std::uint64_t> stream_file_tx(proto::IImageSink& sink, proto::WideIoIndex index,
                                               const proto::SessionParams& params, svc::IFile& f,
                                               std::span<std::uint8_t> buf,
                                               FileTxCounters* counters = nullptr);

}  // namespace mister::cores
