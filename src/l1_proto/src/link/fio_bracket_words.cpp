// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/fio_bracket_words.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/prelude.h"
#include "proto/fio_opcodes.h"
#include "proto/wire_word.h"

namespace mister::proto::link {

namespace {

constexpr std::size_t kExtChars = 4;

constexpr std::uint8_t ascii_upper(std::uint8_t c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<std::uint8_t>(c - 0x20u) : c;
}

[[nodiscard]] Ex<void> write_index_register(ILinkFrame& frame, hal::LinkWord index) {
    if (auto r = frame.begin(FioOp::FileIndex); !r)
        return r;
    if (auto r = frame.word(index); !r)
        return r;
    return frame.end();
}

}

Ex<void> FioBracketWords::set_index(ILinkFrame& frame, FioIndexByte index) {
    return write_index_register(frame, index.wire_word());
}

Ex<void> FioBracketWords::set_index(ILinkFrame& frame, WideIoIndex index) {
    return write_index_register(frame, index.wire_word());
}

Ex<void> FioBracketWords::send_file_info(ILinkFrame& frame, std::string_view ext) {
    std::array<std::uint8_t, kExtChars> c{};
    for (std::size_t i = 0; i < kExtChars && i < ext.size(); ++i) {
        const auto raw = static_cast<std::uint8_t>(ext[i]);
        if (raw == 0)
            break;
        c[i] = ascii_upper(raw);
    }
    const auto w0 = static_cast<std::uint16_t>((c[0] << 8) | c[1]);
    const auto w1 = static_cast<std::uint16_t>((c[2] << 8) | c[3]);

    const std::array<hal::LinkWord, 2> words{hal::LinkWord{w0}, hal::LinkWord{w1}};
    return send_frame(frame, FioOp::FileInfo, words);
}

Ex<void> FioBracketWords::emit_open(ILinkFrame& frame, const FioOpen& open) {
    if (auto r = set_index(frame, open.index); !r)
        return r;
    if (!open.ext.empty()) {
        if (auto r = send_file_info(frame, open.ext.view()); !r)
            return r;
    }
    if (auto r = frame.begin(FioOp::FileTx); !r)
        return r;
    if (auto r = for_window_words(
            kModeDownload,
            open.aux,
            [&frame](hal::LinkWord w) { return frame.word(w); }
        );
        !r) {
        return r;
    }
    return frame.end();
}

Ex<void> FioBracketWords::emit_data(
    ILinkFrame& frame,
    hal::Width w,
    std::span<const std::uint8_t> bytes
) {
    if (auto r = frame.begin(FioOp::FileTxDat); !r)
        return r;
    auto walk = for_each_word(w, bytes, [&frame](hal::LinkWord word) {
        if (auto r = frame.word(word); !r)
            return Ex<bool>{std::unexpected(r.error())};
        return Ex<bool>{true};
    });
    if (!walk)
        return std::unexpected(walk.error());
    return frame.end();
}

Ex<void> FioBracketWords::emit_end(ILinkFrame& frame) {
    if (auto r = frame.begin(FioOp::FileTx); !r)
        return r;
    if (auto r = frame.word(kModeEnd); !r)
        return r;
    return frame.end();
}

}
