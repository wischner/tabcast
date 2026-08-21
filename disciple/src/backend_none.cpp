//
// The backend for a build with no display and no image library.
//
// Selected by BACKEND=none. It exists so the protocol, the panel
// model, and the tests can be built and run anywhere, including a
// machine with no graphics stack at all. It draws nothing and decodes
// nothing, and says so rather than pretending.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/backend.h"

namespace disciple {
namespace {

class no_backend final : public backend {
public:
    const char *name() const override {
        return "none (no display, no decoder)";
    }

    bool can_display() const override {
        return false;
    }

    bool decode(const std::uint8_t *, std::size_t, rgba_image &,
                std::string &error) override {
        error = "this build has no image decoder, rebuild with "
                "BACKEND=test";
        return false;
    }

    int watch(int, const std::function<bool(live_view &)> &,
              int) override {
        // Same reasoning as view(): a headless build being asked to
        // stream is a normal mistake, and the caller has already
        // said nothing will be drawn.
        return 0;
    }

    int view(const panel_frame &, int) override {
        // Not an error. A headless build being asked to show
        // something is a normal thing to do by mistake, and the
        // caller has already reported that nothing will be drawn.
        return 0;
    }

    bool save(const panel_frame &, const std::string &,
              std::string &error) override {
        error = "this build has no image encoder, rebuild with "
                "BACKEND=test";
        return false;
    }
};

} // namespace

std::unique_ptr<backend> make_backend() {
    return std::make_unique<no_backend>();
}

} // namespace disciple
