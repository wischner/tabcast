//
// One deadline and what to do when it passes.
//
// An inkerface program keeps its own time: its main loop waits on the
// buttons for at most as long as the next deadline allows, then checks
// it. The reader only ever needs one deadline at a time (the splash, the
// next discovery, a connect timeout, Apostle falling silent), so that is
// what this is. Setting it again replaces it.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_TIMER_H
#define DISCIPLE_TIMER_H

#include <chrono>
#include <functional>

namespace disciple {

//
// One deadline.
//
class timer {
public:
    using clock = std::chrono::steady_clock;

    //
    // Arm the deadline, replacing any already armed.
    //
    // Parameters:
    //      delay       - From now.
    //      on_due      - Called once, from fire_if_due(), after delay.
    //
    void set(std::chrono::milliseconds delay, std::function<void()> on_due);

    //
    // Disarm. Harmless when not armed.
    //
    void cancel();

    //
    // Time left until the deadline: zero when due, and longest when
    // nothing is armed.
    //
    // Parameters:
    //      longest     - What to report when nothing is armed, and the
    //                    most ever reported.
    //
    std::chrono::milliseconds remaining(std::chrono::milliseconds longest) const;

    //
    // Call the deadline's function if it is due.
    //
    // Notes:
    //      The function is taken out before it is called, so it may set
    //      the timer again.
    //
    void fire_if_due();

private:
    bool _armed = false;
    clock::time_point _due;
    std::function<void()> _on_due;
};

} // namespace disciple

#endif // DISCIPLE_TIMER_H
