//
// One deadline and what to do when it passes.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/timer.h"

#include <algorithm>
#include <utility>

namespace disciple {

void timer::set(std::chrono::milliseconds delay, std::function<void()> on_due) {
    _armed = true;
    _due = clock::now() + delay;
    _on_due = std::move(on_due);
}

void timer::cancel() {
    _armed = false;
    _on_due = nullptr;
}

std::chrono::milliseconds timer::remaining(std::chrono::milliseconds longest) const {
    if (!_armed) {
        return longest;
    }
    const auto left =
        std::chrono::ceil<std::chrono::milliseconds>(_due - clock::now());
    return std::clamp(left, std::chrono::milliseconds(0), longest);
}

void timer::fire_if_due() {
    if (!_armed || clock::now() < _due) {
        return;
    }
    const std::function<void()> call = std::move(_on_due);
    _armed = false;
    _on_due = nullptr;
    call();
}

} // namespace disciple
