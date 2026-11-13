// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/remembered_path.h"

#include <array>
#include <cstring>
#include <span>

#include "infra/byte_view.h"
#include "svc/vfs.h"

namespace mister::app {

std::string remembered_blob_name(const RememberedStem& stem, proto::FileRowId row) {
    std::string n{stem.view()};
    n += (row.kind == proto::FileRowKind::File) ? ".f" : ".s";
    n += row.index.text().c_str();
    return n;
}

std::string read_remembered_path(
    const svc::Vfs& vfs,
    const RememberedStem& stem,
    proto::FileRowId row
) {
    if (stem.empty())
        return {};
    auto f = vfs.open("config/" + remembered_blob_name(stem, row), svc::OpenMode::ReadWhole);
    if (!f)
        return {};
    std::array<std::byte, kRememberedBlob> blob{};
    auto n = (*f)->read_at(0, blob);
    if (!n || *n == 0)
        return {};
    return std::string{infra::text_until_nul(std::span{blob})};
}

}
