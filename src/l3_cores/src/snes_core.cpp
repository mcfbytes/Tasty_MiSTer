// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes_core.h"

#include "cores/manifests/snes.h"
#include "infra/byte_view.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace mister::cores {

namespace {

bool iequal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }
    return true;
}

}

SnesCore::SnesCore(const CoreProfile& p, const HostServices& h)
    : Core(p, h),
      data_host_(kGenericProfile, h),
      mailbox_(MailboxCarriage::granted(h, manifests::kSnesMailboxFrame)) {}

Ex<void> SnesCore::do_adopt(const CoreAdopt&) {

    return {};
}

void SnesCore::on_set_pending_file_ext(std::string_view ext) noexcept {
    (void)ext_.assign(ext);
    data_host_.set_pending_ext(ext);
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
        if (index.v == 0 || iequal(ext, ".SMC") || iequal(ext, ".SFC") || iequal(ext, ".BIN")) {
            arm = Arm::Rom;
        } else if (iequal(ext, ".BS") || iequal(ext, ".SPC")) {
            arm = Arm::Refused;
        }
    }
    if (arm == Arm::Refused)
        return unimplemented(ERR_SITE());
    if (arm == Arm::Rom) {
        if (total > 0xFFFFFFFFu) {
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

std::uint32_t SnesCore::score_candidate_(
    std::span<const std::uint8_t> bytes,
    std::uint32_t base,
    std::uint32_t payload,
    std::uint32_t addr
) noexcept {
    if (static_cast<std::uint64_t>(addr) + snes::kHeaderWindowBytes > payload) {
        return std::uint32_t{0};
    }
    std::array<std::uint8_t, snes::kHeaderWindowBytes> win{};
    const std::size_t got = read_window_(bytes, static_cast<std::uint64_t>(base) + addr, win);
    if (got < win.size())
        return std::uint32_t{0};
    const std::uint16_t resetvector = snes::window_reset_vector(win);
    if (resetvector < 0x8000)
        return std::uint32_t{0};
    const std::uint32_t ro = snes::resetop_addr(addr, resetvector);
    if (ro >= payload)
        return std::uint32_t{0};
    std::uint8_t resetop = 0;
    const std::size_t ro_got = read_window_(
        bytes,
        static_cast<std::uint64_t>(base) + ro,
        std::span(&resetop, 1)
    );
    if (ro_got == 0)
        return std::uint32_t{0};
    return snes::score_window(win, resetop, addr);
}

SnesCore::Analysis SnesCore::analyze_(
    std::span<const std::uint8_t> bytes,
    std::uint32_t base,
    std::uint32_t payload
) noexcept {
    Analysis out{};

    const auto lo = score_candidate_(bytes, base, payload, snes::kLoRomHeader);
    const auto hi = score_candidate_(bytes, base, payload, snes::kHiRomHeader);
    const auto ex = score_candidate_(bytes, base, payload, snes::kExHiRomHeader);
    out.addr = snes::pick_header(lo, hi, ex);

    std::array<std::uint8_t, 64> sig{};
    std::size_t sig_len = 0;
    if (payload > snes::kSignatureProbe) {
        const auto want = static_cast<std::size_t>(
            std::min<std::uint32_t>(64, payload - snes::kSignatureProbe)
        );
        sig_len = read_window_(
            bytes,
            static_cast<std::uint64_t>(base) + snes::kSignatureProbe,
            std::span(sig.data(), want)
        );
    }
    const auto sig_is = [&](const std::uint8_t* magic, std::size_t n) {
        return sig_len >= n && std::memcmp(sig.data(), magic, n) == 0;
    };
    out.sniff.bsx_bios = sig_is(
        infra::text_bytes<const std::uint8_t>(snes::kBsxBiosMagic).data(),
        snes::kBsxBiosMagic.size()
    );
    out.sniff.cc92 = sig_is(snes::kCc92Signature.data(), snes::kCc92Signature.size());
    out.sniff.pf94 = sig_is(snes::kPf94TenKSignature.data(), snes::kPf94TenKSignature.size())
        || sig_is(snes::kPf94OneMSignature.data(), snes::kPf94OneMSignature.size());

    const std::string_view magic = snes::kSufamiMagic;
    const std::string_view backup = snes::kSufamiBackupMagic;
    bool gate = false;
    if (payload >= magic.size()) {
        std::array<std::uint8_t, 14> head{};
        const std::size_t got = read_window_(bytes, base, std::span(head.data(), magic.size()));
        gate = got >= magic.size() && std::memcmp(head.data(), magic.data(), magic.size()) == 0;
    }
    if (gate) {
        for (std::uint64_t offs = 0; offs < payload; offs += snes::kSufamiStride) {
            std::array<std::uint8_t, 32> w{};
            const auto want = static_cast<std::size_t>(
                std::min<std::uint64_t>(w.size(), payload - offs)
            );
            const std::size_t n = read_window_(bytes, base + offs, std::span(w.data(), want));
            if (n < magic.size() || std::memcmp(w.data(), magic.data(), magic.size()) != 0) {
                continue;
            }
            const bool is_backup = n >= snes::kSufamiBackupOffset + backup.size()
                && std::memcmp(w.data() + snes::kSufamiBackupOffset, backup.data(), backup.size())
                    == 0;
            if (is_backup) {
                out.sniff.sufami_bios = true;
            } else {
                if (!out.sniff.sufami_base)
                    out.addr = static_cast<std::uint32_t>(offs);
                out.sniff.sufami_turbo = out.sniff.sufami_base;
                out.sniff.sufami_base = true;
            }
        }
    }
    return out;
}

Ex<void> SnesCore::tx_rom_(IoIndex index, std::span<const std::uint8_t> bytes) {
    const auto file_size = static_cast<std::uint32_t>(bytes.size());
    const std::uint32_t base = snes::copier_offset(file_size);
    const std::uint32_t payload = file_size - base;

    const Analysis analysis = analyze_(bytes, base, payload);
    const std::uint32_t addr = analysis.addr;
    last_header_addr_ = addr;

    snes::TypingBytes typing{};
    if (addr != 0) {
        const std::uint32_t win_base = addr >= snes::kTypingWindowBack
            ? addr - snes::kTypingWindowBack
            : 0;
        std::array<std::uint8_t, snes::kTypingWindowBytes> win{};
        std::size_t win_len = 0;
        if (win_base < payload) {
            const auto want = static_cast<std::size_t>(
                std::min<std::uint32_t>(snes::kTypingWindowBytes, payload - win_base)
            );
            win_len = read_window_(
                bytes,
                static_cast<std::uint64_t>(base) + win_base,
                std::span(win.data(), want)
            );
        }
        typing = snes::type_cart(
            std::span<const std::uint8_t>(win.data(), win_len),
            win_base,
            addr,
            payload,
            analysis.sniff
        );
    }
    const auto hdr = snes::header_block(addr, payload, addr != 0 ? &typing : nullptr);

    proto::SessionParams params{};
    params.ext = ext_.view();
    auto session = proto::ImageBracket::open(image_sink(), proto::WideIoIndex{index.v}, params);
    if (!session)
        return std::unexpected(session.error());
    last_index_ = static_cast<std::uint16_t>(index.v);
    ++transfers_;
    ++rom_transforms_;

    if (auto r = session->write(hdr); !r)
        return r;
    bytes_sent_ += hdr.size();

    if (auto r = stream_mirrored_(*session, bytes, base, payload); !r)
        return r;
    return session->end();
}

Ex<void> SnesCore::stream_mirrored_(
    proto::ImageBracket& session,
    std::span<const std::uint8_t> bytes,
    std::uint32_t base,
    std::uint32_t payload
) {
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
                bytes,
                static_cast<std::uint64_t>(base) + src,
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

    auto session = proto::ImageBracket::open(image_sink(), proto::WideIoIndex{index.v}, params);
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
