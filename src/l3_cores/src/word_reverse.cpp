// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/word_reverse.h"

namespace mister::cores::n64 {

template class WordReverseT<seat_owner::Main>;
template class WordReverseT<seat_owner::Io>;

}
