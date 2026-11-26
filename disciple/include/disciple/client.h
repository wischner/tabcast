//
// The Disciple reader's entry logic.
//
// Kept separate from the entry point, src/main.cpp, which is inkerface's
// program() and does nothing but call this, so that the tests can link
// everything else.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_CLIENT_H
#define DISCIPLE_CLIENT_H

namespace disciple {

//
// Run the reader until its input closes.
//
// Parameters:
//      argc, argv  - Arguments as handed to program(). argc is 0 on a
//                    reader.
//
// Returns:
//      A process exit code. On a reader it never returns: the input
//      never closes.
//
int run(int argc, char *argv[]);

} // namespace disciple

#endif // DISCIPLE_CLIENT_H
