// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/bitstream_programmer.h"

#include "infra/seat.h"

#include <algorithm>
#include <cstring>

#include "app/stream_io.h"
#include "hal/bridge_sequencer.h"
#include "hal/fpga_programmer.h"
#include "svc/vfs.h"

namespace mister::app {

[[nodiscard]] Ex<PayloadWindow> container_window(std::span<const std::byte> hdr,
                                                 std::uint64_t fsz) {
    static constexpr char kMagic[6] = {'M', 'i', 'S', 'T', 'e', 'r'};
    if (fsz < kContainerHeaderBytes || hdr.size() < kContainerHeaderBytes ||
        std::memcmp(hdr.data(), kMagic, sizeof(kMagic)) != 0) {
        return PayloadWindow{0, fsz};
    }
    const auto b = [hdr](std::size_t i) {
        return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(hdr[i]));
    };
    const std::uint32_t declared = b(12) | (b(13) << 8) | (b(14) << 16) | (b(15) << 24);
    if (declared > fsz - kContainerHeaderBytes) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), declared});
    }
    return PayloadWindow{kContainerHeaderBytes, declared};
}

Ex<void> BitstreamProgrammer::program(const ParkReceipt&, std::string_view rel) {
    return run_(rel);
}

Ex<void> BitstreamProgrammer::program_at_boot(std::string_view rel) { return run_(rel); }

bool BitstreamProgrammer::programmed() const { return programmer_->programmed(); }

Ex<void> BitstreamProgrammer::run_(std::string_view rel) {
    auto src = stream_open(*vfs_, rel, svc::OpenMode::ReadWhole);
    if (!src) return std::unexpected(src.error());

    PayloadWindow win{0, src->size};
    if (src->size >= kContainerHeaderBytes) {
        std::byte hdr[kContainerHeaderBytes];
        if (auto r = stream_read(*src->f, 0, std::span<std::byte>(hdr, sizeof(hdr))); !r) {
            return std::unexpected(r.error());
        }
        auto w = container_window(std::span<const std::byte>(hdr, sizeof(hdr)), src->size);
        if (!w) return std::unexpected(w.error());
        win = *w;
    }

    if (win.length == 0) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    if (auto r = bridges_->pre_program(); !r) return std::unexpected(r.error());
    bridges_down_ = true;
    if (auto r = programmer_->drive_to_rest(programmer_->program_begin()); !r) {
        return std::unexpected(r.error());
    }

    for (std::uint64_t pos = 0; pos < win.length;) {
        const auto n =
            static_cast<std::size_t>(std::min<std::uint64_t>(chunk_bytes_, win.length - pos));
        if (auto r = stream_read(*src->f, win.base + pos, std::span<std::byte>(buf_, n)); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = programmer_->program_chunk(std::span<const std::byte>(buf_, n)); !r) {
            return std::unexpected(r.error());
        }
        pos += n;
    }

    if (auto r = programmer_->drive_to_rest(programmer_->program_end()); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = bridges_->post_program(); !r) return std::unexpected(r.error());
    bridges_down_ = false;
    return {};
}

}  // namespace mister::app
