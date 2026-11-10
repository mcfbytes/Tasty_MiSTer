// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/disc_save_migration.h"

namespace mister::cores {

SavePath pick_disc_path(
    const svc::Vfs& vfs,
    const SavePath& own,
    const SavePath& stock,
    proto::FileSize length
) {
    if (vfs.file_exists(own.view()) || !vfs.file_exists(stock.view()))
        return own;
    auto f = vfs.open(stock.view(), svc::OpenMode::Read);
    if (!f)
        return own;
    auto size = (*f)->size();
    if (!size || *size != length)
        return own;
    return stock;
}

}
