// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "cores/mdp_cue.h"
#include "cores/pcm_extent.h"
#include "cores/pcm_source.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::cores::mdp {

class CueWavSource final : public IPcmSource {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    static constexpr std::uint32_t kMaxCueBytes = 256u * 1024u;

    static constexpr std::uint32_t kSectorBytes = 2352;

    CueWavSource(const svc::Vfs& vfs, std::string_view rom_path) noexcept;

    [[nodiscard]] Ex<PcmExtent> open(std::uint8_t track) override;
    [[nodiscard]] Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) override;
    void close() noexcept override;

    [[nodiscard]] std::string_view cue_path() const noexcept { return cue_.view(); }
    [[nodiscard]] std::string_view base_dir() const noexcept { return base_.view(); }
    [[nodiscard]] std::uint8_t track_count() const noexcept { return sheet_.track_count(); }
    [[nodiscard]] bool parsed() const noexcept { return parsed_; }

private:
    [[nodiscard]] Ex<void> ensure_parsed_();
    [[nodiscard]] Ex<void> locate_data_(svc::IFile& f, std::uint32_t file_size);

    const svc::Vfs* vfs_;
    FixedStr<CueSheet::kPathMax, StrFit::Clip> cue_{};
    FixedStr<CueSheet::kPathMax, StrFit::Clip> base_{};
    CueSheet sheet_{};
    std::unique_ptr<svc::IFile> wav_{};
    std::uint32_t data_start_ = 0;
    std::uint32_t data_size_ = 0;
    bool parsed_ = false;
};

}  // namespace mister::cores::mdp
