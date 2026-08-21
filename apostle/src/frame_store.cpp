//
// The newest captured frame, and nothing older.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "apostle/frame_store.h"

#include <utility>

namespace apostle {

void frame_store::put(frame value) {
    _newest = std::move(value);
    _have = true;
    ++_accepted;
}

const frame *frame_store::newest() const {
    return _have ? &_newest : nullptr;
}

long long frame_store::accepted() const {
    return _accepted;
}

} // namespace apostle
