// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes/snes_loader.h"
#include "infra/span_cut.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include "cores/snes/snes_rom_file.h"
#include "infra/ascii.h"
#include "infra/path_parts.h"
#include "infra/prelude.h"

namespace mister::cores::snes {

namespace detail {

SnesRomFile::SnesRomFile(
    std::unique_ptr<svc::IFile> raw,
    ImageExtent extent,
    std::array<std::uint8_t, kHeaderBlockBytes> header
)
    : raw_(std::move(raw)),
      extent_(extent),
      header_(header) {}

Ex<infra::ByteCount> SnesRomFile::read_at(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(SnesRomFile);
    const proto::FileSize whole = proto::FileSize{kHeaderBlockBytes} + extent_.padded;
    if (!whole.holds(at))
        return infra::ByteCount{};
    const proto::FileSize want = at.remaining_in(whole).min(proto::FileSize::of(dst));
    std::span<std::byte> out = dst;
    proto::FileSize left = want;
    proto::FileOffset here = at;
    if (here < kHeaderEnd) {
        const std::span<const std::uint8_t> from = infra::affine::from(
            std::span<const std::uint8_t>(header_),
            here
        );
        const std::span<const std::uint8_t> part = infra::affine::first(
            from,
            (kHeaderEnd - here).min(left)
        );
        std::memcpy(out.data(), part.data(), part.size());
        out = out.subspan(part.size());
        left = left.after_header(proto::FileSize::of(part));
        here = kHeaderEnd;
    }
    while (!left.empty()) {
        const proto::FileSize pos = here - kHeaderEnd;
        proto::FileSize src = pos;
        proto::FileSize take = left;
        if (pos >= extent_.payload) {
            const MirrorRun run = mirror_run(
                pos.file_word<std::uint32_t>(),
                extent_.payload.file_word<std::uint32_t>(),
                extent_.padded.file_word<std::uint32_t>()
            );
            src = proto::FileSize{run.src};
            take = take.min(proto::FileSize{run.len});
        } else {
            take = take.min(extent_.payload - pos);
        }
        const std::span<std::byte> piece = infra::affine::first(out, take);
        if (auto r = raw_->read_exact_at(proto::kFileStart + (extent_.image_at + src), piece); !r) {
            return std::unexpected(r.error());
        }
        out = out.subspan(piece.size());
        left = left.after_header(proto::FileSize::of(piece));
        here = here + take;
    }
    return infra::ByteCount{infra::affine::first(dst, want - left).size()};
}

Ex<infra::ByteCount> SnesRomFile::write_at(proto::FileOffset, std::span<const std::byte>) {
    TASTY_SEAT_BODY(SnesRomFile);
    return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
}

Ex<proto::FileSize> SnesRomFile::size() const {
    TASTY_SEAT_BODY(SnesRomFile);
    return proto::FileSize{kHeaderBlockBytes} + extent_.padded;
}

Ex<void> SnesRomFile::flush() {
    TASTY_SEAT_BODY(SnesRomFile);
    return {};
}

}

Ex<LoadPlan> SnesLoader::plan(const LoadAsk&, const proto::StatusWord&) {
    TASTY_SEAT_BODY(SnesLoader);
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

SnesLoader::Analysis SnesLoader::analyze_(std::span<const std::uint8_t> image) noexcept {
    Analysis out{};

    out.addr = find_header(image);

    const auto sig = kSignatureProbe.slice(image, kImageStart, kHeaderWindow);
    const auto sig_is = [&](std::span<const std::uint8_t> magic) {
        return sig.size() >= magic.size() && std::ranges::equal(sig.first(magic.size()), magic);
    };
    out.sniff.bsx_bios = sig_is(infra::text_bytes<const std::uint8_t>(kBsxBiosMagic));
    out.sniff.cc92 = sig_is(kCc92Signature);
    out.sniff.pf94 = sig_is(kPf94TenKSignature) || sig_is(kPf94OneMSignature);

    const SufamiScan scan = sufami_walk(image);
    out.sniff.sufami_bios = scan.bios;
    out.sniff.sufami_base = scan.base;
    out.sniff.sufami_turbo = scan.turbo;
    if (scan.base)
        out.addr = scan.addr;
    return out;
}

Ex<std::unique_ptr<svc::IFile>> SnesLoader::source(
    const LoadAsk& ask,
    std::unique_ptr<svc::IFile> raw
) {
    TASTY_SEAT_BODY(SnesLoader);
    if (raw == nullptr)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});

    if (ask.size.empty())
        return raw;
    const std::string_view ext = infra::suffix_from_last_dot(ask.path);
    if (ext.empty())
        return raw;
    const bool rom_arm = ask.index.fio_byte() == proto::FioIndexByte{}
        || infra::ieq(ext, ".SMC")
        || infra::ieq(ext, ".SFC")
        || infra::ieq(ext, ".BIN");
    if (!rom_arm) {
        if (infra::ieq(ext, ".BS") || infra::ieq(ext, ".SPC"))
            return unimplemented(ERR_SITE());
        return raw;
    }

    constexpr proto::FileSize kMaxRomBytes{std::numeric_limits<std::uint32_t>::max()};
    if (ask.size > kMaxRomBytes)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});

    const std::uint32_t base = copier_offset(ask.size);
    const proto::FileSize payload = ask.size.after_header(proto::FileSize{base});
    std::vector<std::uint8_t> image = payload.zeroed<std::vector<std::uint8_t>>();
    if (!image.empty()) {
        if (auto r = raw->read_exact_at(
                proto::kFileStart + proto::FileSize{base},
                std::as_writable_bytes(std::span(image))
            );
            !r) {
            return std::unexpected(r.error());
        }
    }

    const Analysis analysis = analyze_(image);
    TypingBytes typing{};
    const bool typed = analysis.addr != kNoHeader;
    if (typed) {
        typing = type_cart(
            typing_window(image, analysis.addr),
            analysis.addr,
            payload.file_word<std::uint32_t>(),
            analysis.sniff
        );
    }
    const auto header = header_block(
        analysis.addr,
        payload.file_word<std::uint32_t>(),
        typed ? &typing : nullptr
    );
    const std::uint32_t padded = mirrored_size(payload.file_word<std::uint32_t>());
    return std::make_unique<detail::SnesRomFile>(
        std::move(raw),
        detail::ImageExtent{
            .image_at = proto::FileSize{base},
            .payload = payload,
            .padded = proto::FileSize{padded}
        },
        header
    );
}

}
