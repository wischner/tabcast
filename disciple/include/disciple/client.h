//
// The Disciple client entry logic.
//
// Kept separate from the process entry point, which is src/main.cpp
// and does nothing but call this. That keeps everything the client
// does linkable into a test binary, which brings its own main().
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#ifndef DISCIPLE_CLIENT_H
#define DISCIPLE_CLIENT_H

namespace disciple {

//
// Run the client.
//
// Parameters:
//      argc, argv  - Arguments as handed to the entry point.
//
// Returns:
//      A process exit code.
//
int run(int argc, char *argv[]);

} // namespace disciple

#endif // DISCIPLE_CLIENT_H
