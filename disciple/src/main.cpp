//
// Disciple's entry point: inkerface's program().
//
// inkerface provides the real one, main() on a desktop and Arduino
// setup() on a reader, and calls this. Nothing but the call is here, so
// everything the reader does can be linked into the tests.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/client.h"

#include <inkerface/program.h>

int program(int argc, char **argv) {
    return disciple::run(argc, argv);
}
