// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes_core.h"
#include "cores/core_adopt.h"

#include "cores/manifests/snes.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace mister::cores {

SnesCore::SnesCore(const CoreProfile& p, const HostServices& h)
    : Core(p, h),
      mailbox_(MailboxCarriage::granted(h, manifests::kSnesMailboxFrame)) {}

Ex<void> SnesCore::do_adopt(const CoreAdopt&) {

    return {};
}

void SnesCore::on_set_pending_file_ext(std::string_view ext) noexcept {
    (void)ext_.assign(ext);
    stream_.set_pending_ext(ext);
}

Ex<void> SnesCore::on_mount(IoIndex, const MountedPath&) {

    return unimplemented(ERR_SITE());
}

Ex<void> SnesCore::on_file_tx(IoIndex index, std::span<const std::uint8_t> bytes) {
    const std::uint64_t total = bytes.size();
    bytes_sent_ = 0;

    enum class Arm : std::uint8_t { Raw, Rom, Refused };
    Arm arm = Arm::Raw;
    const std::string_view ext = ext_.view();
    if (total != 0 && !ext.empty()) {
        if (index == IoIndex{}
            || infra::ieq(ext, ".SMC")
            || infra::ieq(ext, ".SFC")
            || infra::ieq(ext, ".BIN")) {
            arm = Arm::Rom;
        } else if (infra::ieq(ext, ".BS") || infra::ieq(ext, ".SPC")) {
            arm = Arm::Refused;
        }
    }
    if (arm == Arm::Refused)
        return unimplemented(ERR_SITE());
    if (arm == Arm::Rom) {
        if (total > std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
        }
        return tx_rom_(index, bytes);
    }
    return tx_raw_(index, bytes);
}

std::size_t SnesCore::read_window_(
    std::span<const std::uint8_t> bytes,
    std::uint64_t off,
    std::span<std::uint8_t> dst
) noexcept {
    if (off >= bytes.size())
        return 0;
    const std::size_t n = std::min(dst.size(), bytes.size() - static_cast<std::size_t>(off));
    std::memcpy(dst.data(), bytes.data() + off, n);
    return n;
}

SnesCore::Analysis SnesCore::analyze_(std::span<const std::uint8_t> image) noexcept {
    Analysis out{};

    out.addr = snes::find_header(image);

    const auto sig = snes::kSignatureProbe.slice(image, snes::kImageStart, snes::kHeaderWindow);
    const auto sig_is = [&](std::span<const std::uint8_t> magic) {
        return sig.size() >= magic.size() && std::ranges::equal(sig.first(magic.size()), magic);
    };
    out.sniff.bsx_bios = sig_is(infra::text_bytes<const std::uint8_t>(snes::kBsxBiosMagic));
    out.sniff.cc92 = sig_is(snes::kCc92Signature);
    out.sniff.pf94 = sig_is(snes::kPf94TenKSignature) || sig_is(snes::kPf94OneMSignature);

    const snes::SufamiScan scan = snes::sufami_walk(image);
    out.sniff.sufami_bios = scan.bios;
    out.sniff.sufami_base = scan.base;
    out.sniff.sufami_turbo = scan.turbo;
    if (scan.base)
        out.addr = scan.addr;
    return out;
}

Ex<void> SnesCore::tx_rom_(IoIndex index, std::span<const std::uint8_t> bytes) {
    const auto file_size = static_cast<std::uint32_t>(bytes.size());
    const std::uint32_t base = snes::copier_offset(proto::FileSize{file_size});
    const std::uint32_t payload = file_size - base;

    const std::span<const std::uint8_t> image = base <= bytes.size() ? bytes.subspan(base)
                                                                     : bytes.first(0);

    const Analysis analysis = analyze_(image);
    const snes::HeaderAddr addr = analysis.addr;
    last_header_addr_ = addr;

    snes::TypingBytes typing{};
    const bool typed = addr != snes::kNoHeader;
    if (typed) {
        typing = snes::type_cart(snes::typing_window(image, addr), addr, payload, analysis.sniff);
    }
    const auto hdr = snes::header_block(addr, payload, typed ? &typing : nullptr);

    proto::SessionParams params{};
    params.ext = ext_.view();
    auto session = proto::ImageBracket::open(
        image_sink(),
        proto::WideIoIndex::of(index, 0),
        params
    );
    if (!session)
        return std::unexpected(session.error());
    last_index_ = static_cast<std::uint16_t>(index.v);
    ++transfers_;
    ++rom_transforms_;

    if (auto r = session->write(hdr); !r)
        return r;
    bytes_sent_ += hdr.size();

    if (auto r = stream_mirrored_(*session, image); !r)
        return r;
    return session->end();
}

Ex<void> SnesCore::stream_mirrored_(
    proto::ImageBracket& session,
    std::span<const std::uint8_t> image
) {
    const auto payload = static_cast<std::uint32_t>(image.size());
    const std::uint32_t padded = snes::mirrored_size(payload);
    std::uint32_t pos = 0;
    while (pos < padded) {
        const auto chunk = std::min<std::uint32_t>(kChunkBytes, padded - pos);
        std::uint32_t filled = 0;
        while (filled < chunk) {
            const std::uint32_t at = pos + filled;
            std::uint32_t src = at;
            std::uint32_t avail = payload - at;
            if (at >= payload) {
                const snes::MirrorRun run = snes::mirror_run(at, payload, padded);
                src = run.src;
                avail = run.len;
            }
            const std::uint32_t take = std::min(avail, chunk - filled);
            const std::size_t got = read_window_(
                image,
                src,
                std::span<std::uint8_t>(buf_ + filled, static_cast<std::size_t>(take))
            );
            filled += static_cast<std::uint32_t>(got);
        }
        if (auto r = session.write(std::span<const std::uint8_t>(buf_, chunk)); !r)
            return r;
        bytes_sent_ += chunk;
        pos += chunk;
    }
    return {};
}

Ex<void> SnesCore::tx_raw_(IoIndex index, std::span<const std::uint8_t> bytes) {
    proto::SessionParams params{};
    params.ext = ext_.view();

    auto session = proto::ImageBracket::open(
        image_sink(),
        proto::WideIoIndex::of(index, 0),
        params
    );
    if (!session)
        return std::unexpected(session.error());
    last_index_ = static_cast<std::uint16_t>(index.v);
    ++transfers_;

    std::uint64_t remaining = bytes.size();
    std::uint64_t off = 0;
    while (remaining != 0) {
        const auto chunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, kChunkBytes)
        );
        const std::size_t got = read_window_(bytes, off, std::span<std::uint8_t>(buf_, chunk));
        if (auto r = session->write(std::span<const std::uint8_t>(buf_, got)); !r) {
            return r;
        }
        bytes_sent_ += got;
        off += got;
        remaining -= got;
    }
    return session->end();
}

}
