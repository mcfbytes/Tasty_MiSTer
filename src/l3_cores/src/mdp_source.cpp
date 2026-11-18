// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mdp_source.h"

#include <cstring>
#include <limits>
#include <vector>

#include "cores/wav_data.h"
#include "infra/path_parts.h"
#include "svc/track.h"

namespace mister::cores::mdp {

constexpr proto::FileSize kMaxCueFileBytes{CueWavSource::kMaxCueBytes};
constexpr proto::FileSize kMaxWavBytes{std::numeric_limits<std::uint32_t>::max()};

CueWavSource::CueWavSource(const svc::Vfs& vfs, std::string_view rom_path) noexcept : vfs_(&vfs) {

    const std::string_view stem = infra::cut_at_last_dot(rom_path);
    (void)cue_.assign(stem);
    (void)cue_.append(".cue");
    (void)base_.assign(infra::dir_or_self(rom_path));
}

Ex<void> CueWavSource::ensure_parsed_() {
    if (parsed_)
        return {};
    auto f = vfs_->open(cue_.view(), svc::OpenMode::ReadWhole);
    if (!f)
        return std::unexpected(f.error());
    auto sz = (*f)->size();
    if (!sz)
        return std::unexpected(sz.error());
    if (*sz > kMaxCueFileBytes) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), kMaxCueBytes});
    }
    std::vector<char> text = sz->zeroed<std::vector<char>>();
    if (!text.empty()) {
        auto got = (*f)->read_at(proto::kFileStart, std::as_writable_bytes(std::span<char>(text)));
        if (!got)
            return std::unexpected(got.error());
        text.resize(*got);
    }
    sheet_.parse(std::string_view(text.data(), text.size()), base_.view());
    parsed_ = true;
    return {};
}

Ex<void> CueWavSource::locate_data_(svc::IFile& f, proto::FileSize file_size) {
    proto::FileOffset pos = proto::kFileStart + wav::kPrefix;
    std::byte header[wav::kChunkHeaderBytes]{};
    while (wav::can_read_header(pos, file_size)) {
        auto got = f.read_at(pos, std::span<std::byte>(header));
        if (!got)
            return std::unexpected(got.error());
        if (*got < sizeof header)
            break;
        const std::uint32_t size = wav::declared_size(header);
        if (wav::is_data_chunk(header)) {
            data_start_ = pos + wav::kChunkHeader;
            data_size_ = size;
            return {};
        }

        const proto::FileOffset next = pos + wav::kChunkHeader + proto::FileSize{size};
        if (next > wav::kRiffPositionLimit) {
            return std::unexpected(
                Error{
                    Errc::bad_format,
                    ERR_SITE(),
                    static_cast<std::uint32_t>(wav::Refusal::kSizeOverflows)
                }
            );
        }
        pos = next;
    }
    return std::unexpected(
        Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(wav::Refusal::kNoDataChunk)}
    );
}

Ex<PcmExtent> CueWavSource::open(svc::TrackNumber track) {
    TASTY_SEAT_BODY(CueWavSource);
    close();
    if (auto r = ensure_parsed_(); !r)
        return std::unexpected(r.error());
    const auto t = sheet_.track(track);
    if (!t || t->wav_path.empty()) {
        return std::unexpected(track.refusal(Errc::not_found, ERR_SITE()));
    }
    auto f = vfs_->open(t->wav_path.view(), svc::OpenMode::Read);
    if (!f)
        return std::unexpected(f.error());
    auto sz = (*f)->size();
    if (!sz)
        return std::unexpected(sz.error());
    if (*sz > kMaxWavBytes)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    if (auto r = locate_data_(**f, *sz); !r) {
        return std::unexpected(r.error());
    }
    wav_ = std::move(*f);
    PcmExtent ex{};
    ex.bytes = data_size_;

    ex.loop_byte = static_cast<std::uint32_t>(t->loop_sector * svc::kRawFrameBytes);
    ex.loops = t->loops;
    return ex;
}

Ex<std::span<std::byte>> CueWavSource::read_at(proto::FileSize into, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(CueWavSource);
    if (wav_ == nullptr)
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    auto got = wav_->read_at(data_start_ + into, dst);
    if (!got)
        return std::unexpected(got.error());
    return dst.first(*got);
}

void CueWavSource::close() noexcept {
    TASTY_SEAT_BODY(CueWavSource);
    wav_.reset();
    data_start_ = proto::kFileStart;
    data_size_ = 0;
}

}
