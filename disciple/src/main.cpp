//
// Disciple entry point.
//
// Nothing but main(). Everything it does lives in client.cpp, which
// knows nothing about how the process was started and can therefore be
// built and tested without one.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include "disciple/client.h"

int main(int argc, char *argv[]) {
    return disciple::run(argc, argv);
}
