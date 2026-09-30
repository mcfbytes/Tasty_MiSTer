// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mdp_source.h"

#include <cstring>
#include <string>
#include <vector>

#include "cores/wav_data.h"

namespace mister::cores::mdp {

namespace {

std::string cue_path_of(std::string_view rom_path) {
    const std::size_t dot = rom_path.rfind('.');
    std::string out{dot == std::string_view::npos ? rom_path : rom_path.substr(0, dot)};
    out += ".cue";
    return out;
}

std::string_view base_dir_of(std::string_view rom_path) {
    const std::size_t slash = rom_path.rfind('/');
    return slash == std::string_view::npos ? rom_path : rom_path.substr(0, slash);
}

}  // namespace

CueWavSource::CueWavSource(const svc::Vfs& vfs, std::string_view rom_path) noexcept : vfs_(&vfs) {
    (void)cue_.assign(cue_path_of(rom_path));
    (void)base_.assign(base_dir_of(rom_path));
}

Ex<void> CueWavSource::ensure_parsed_() {
    if (parsed_) return {};
    auto f = vfs_->open(cue_.view(), svc::OpenMode::ReadWhole);
    if (!f) return std::unexpected(f.error());
    auto sz = (*f)->size();
    if (!sz) return std::unexpected(sz.error());
    if (sz->v > kMaxCueBytes) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), kMaxCueBytes});
    }
    std::vector<char> text(static_cast<std::size_t>(sz->v), '\0');
    if (!text.empty()) {
        auto got = (*f)->read_at(0, std::as_writable_bytes(std::span<char>(text)));
        if (!got) return std::unexpected(got.error());
        text.resize(*got);
    }
    sheet_.parse(std::string_view(text.data(), text.size()), base_.view());
    parsed_ = true;
    return {};
}

Ex<void> CueWavSource::locate_data_(svc::IFile& f, std::uint32_t file_size) {
    std::uint32_t pos = wav::kPrefixBytes;
    std::byte header[wav::kChunkHeaderBytes]{};
    while (wav::can_read_header(pos, file_size)) {
        auto got = f.read_at(pos, std::span<std::byte>(header));
        if (!got) return std::unexpected(got.error());
        if (*got < sizeof header) break;
        const std::uint32_t size = wav::declared_size(std::span<const std::byte>(header));
        if (wav::is_data_chunk(std::span<const std::byte>(header))) {
            data_start_ = pos + wav::kChunkHeaderBytes;
            data_size_ = size;
            return {};
        }

        const std::uint64_t next = static_cast<std::uint64_t>(pos) + wav::kChunkHeaderBytes + size;
        if (next > 0xFFFF'FFFFu) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(),
                                         static_cast<std::uint32_t>(wav::Refusal::kSizeOverflows)});
        }
        pos = static_cast<std::uint32_t>(next);
    }
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(),
                                 static_cast<std::uint32_t>(wav::Refusal::kNoDataChunk)});
}

Ex<PcmExtent> CueWavSource::open(std::uint8_t track) {
    TASTY_SEAT_BODY(CueWavSource);
    close();
    if (auto r = ensure_parsed_(); !r) return std::unexpected(r.error());
    const CueSheet::Track* t = sheet_.track(track);
    if (t == nullptr || t->wav_path.empty()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), track});
    }
    auto f = vfs_->open(t->wav_path.view(), svc::OpenMode::Read);
    if (!f) return std::unexpected(f.error());
    auto sz = (*f)->size();
    if (!sz) return std::unexpected(sz.error());
    if (sz->v > 0xFFFF'FFFFu) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    if (auto r = locate_data_(**f, static_cast<std::uint32_t>(sz->v)); !r) {
        return std::unexpected(r.error());
    }
    wav_ = std::move(*f);
    PcmExtent ex{};
    ex.bytes = data_size_;

    ex.loop_byte = static_cast<std::uint32_t>(t->loop_sector * kSectorBytes);
    ex.loops = t->loops;
    return ex;
}

Ex<std::size_t> CueWavSource::read_at(std::uint64_t off, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(CueWavSource);
    if (wav_ == nullptr) return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    return wav_->read_at(static_cast<std::uint64_t>(data_start_) + off, dst);
}

void CueWavSource::close() noexcept {
    TASTY_SEAT_BODY(CueWavSource);
    wav_.reset();
    data_start_ = 0;
    data_size_ = 0;
}

}  // namespace mister::cores::mdp
