// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/bitstream_programmer.h"

#include "infra/seat.h"

#include <algorithm>
#include <cstring>

#include "app/stream_io.h"
#include "hal/bridge_sequencer.h"
#include "infra/endian.h"
#include "hal/fpga_programmer.h"
#include "svc/vfs.h"

namespace mister::app {

[[nodiscard]] Ex<PayloadWindow> container_window(
    std::span<const std::byte> hdr,
    proto::FileSize fsz
) {
    static constexpr char kMagic[6] = {'M', 'i', 'S', 'T', 'e', 'r'};
    static constexpr std::size_t kDeclaredAt = 12;
    if (fsz < kContainerHeader
        || hdr.size() < kContainerHeaderBytes
        || std::memcmp(hdr.data(), kMagic, sizeof(kMagic)) != 0) {
        return PayloadWindow{proto::FileSize{}, fsz};
    }
    const auto word = infra::load_le<std::uint32_t>(
        hdr.subspan<kDeclaredAt, sizeof(std::uint32_t)>()
    );
    const proto::FileSize declared{word};
    if (declared > fsz - kContainerHeader) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), word});
    }
    return PayloadWindow{kContainerHeader, declared};
}

[[nodiscard]] Ex<PayloadWindow> read_payload_window(svc::IFile& f, proto::FileSize size) {
    if (size < kContainerHeader)
        return PayloadWindow{proto::FileSize{}, size};
    std::array<std::byte, kContainerHeaderBytes> hdr{};
    if (auto r = stream_read(f, proto::FileSize{}, std::span<std::byte>(hdr)); !r) {
        return std::unexpected(r.error());
    }
    return container_window(std::span<const std::byte>(hdr), size);
}

Ex<void> BitstreamProgrammer::program(const ParkReceipt&, AllLeasesHome, std::string_view rel) {
    return run_(rel);
}

Ex<void> BitstreamProgrammer::program_at_boot(AllLeasesHome, std::string_view rel) {
    return run_(rel);
}

bool BitstreamProgrammer::programmed() const {
    return programmer_->programmed();
}

Ex<void> BitstreamProgrammer::run_(std::string_view rel) {
    auto src = stream_open(*vfs_, rel, svc::OpenMode::ReadWhole);
    if (!src)
        return std::unexpected(src.error());

    auto win = read_payload_window(*src->f, src->size);
    if (!win)
        return std::unexpected(win.error());

    if (win->length == proto::FileSize{}) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    if (auto r = bridges_->pre_program(); !r)
        return std::unexpected(r.error());
    bridges_down_ = true;
    if (auto r = programmer_->drive_to_rest(programmer_->program_begin()); !r) {
        return std::unexpected(r.error());
    }

    for (proto::FileSize pos{}; pos < win->length;) {
        const auto left = std::min(win->length - pos, proto::FileSize{chunk_bytes_});
        const auto n = static_cast<std::size_t>(left.v);
        if (auto r = stream_read(*src->f, win->base + pos, std::span<std::byte>(buf_.data(), n));
            !r) {
            return std::unexpected(r.error());
        }
        if (auto r = programmer_->program_chunk(std::span<const std::byte>(buf_.data(), n)); !r) {
            return std::unexpected(r.error());
        }
        pos += proto::FileSize{n};
    }

    if (auto r = programmer_->drive_to_rest(programmer_->program_end()); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = bridges_->post_program(); !r)
        return std::unexpected(r.error());
    bridges_down_ = false;
    return {};
}

}
