// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64/word_reverse.h"

#include "infra/prelude.h"

namespace mister::cores::n64 {

template class WordReverseT<seat_owner::Main>;
template class WordReverseT<seat_owner::Io>;

}
