# CPP-CODING-STYLE.md - C++ Coding Standards and Project Rules

This document defines the rules that all agents, including AI agents,
must follow when writing, reviewing, or modifying code in this
project.

We are writing modern C++ code.

- All development and debugging happens  on Linux with GCC.
- GCC builds must use full warnings and sanitizers while debugging.

## 1. Project Directory Structure

Do not create files outside the approved project directories.
Do not leave editor backups, stray object files, or temporary files in
the tree.

- `src/` contains application `.cpp` files.
- `include/` contains all public `.h` files.
- `lib/` contains project libraries and third-party libraries.
- `lib/<name>/` contains the implementation of a library such as
  `lib/dbf/`.
- `bin/` contains final programs and other final generated outputs.
- `build/` contains all build artifacts, including object files,
  dependency files, static libraries, maps, listings, and temporary
  build products.

In a multi-project repository there is exactly **one** `build/` and
**one** `bin/`, both at the repository root, shared by every project.
A project does not get its own pair. Each writes into
`build/<project>/<build type>/` and drops its finished program in
`bin/`. Paths are relative to a `ROOT_DIR` variable, defaulting to
`..`, so building from inside a project directory puts artifacts in
the same place as building from the top.
- `docs/` contains project notes and design documents.
- `scripts/` contains toolchains and developer automation organized by
  purpose or target platform.
- `tests/` contains automated tests.
- `tests/data/` contains data files used by tests.

Rules:

- Public headers must live in `include/`.
- Project source files may live in `src/` or in a library directory
  under `lib/`.
- Build artifacts must go only into the repository's single root
  `build/` directory.
- No project-level `build/` or `bin/` directories are allowed. A
  `build/` appearing beside a project's sources is a bug.
- Nothing generated may be written beside the sources. A clean
  checkout plus `make distclean` must leave no untracked files.
- Nothing else should be created at the project root except normal
  project files such as `LICENSE`, `README.md`, `Makefile`, and
  similar top-level metadata.

## 2. Naming Conventions

- Use lowercase snake_case for C/C++ symbols including classes, etc.
- Do not use Hungarian notation, PascalCase, or camelCase.
- Use uppercase only when a true macro-style constant makes sense.
- Use lowercase snake_case for file names.

Examples:

- Good: `screen_clear()`, `player_score`, `dbf_open()`
- Bad: `ScreenClear()`, `playerScore`, `DbfOpen()`

## 3. Header and Implementation Separation

Every module must follow a classic C++ layout.

- A public `.h` file in `include/` contains:
  - A file header comment
  - Public declarations
  - Public types
  - Public constants or macros
  - Documentation for every public function
- A `.cpp` file in `src/` or `lib/<name>/` contains:
  - A file header comment
  - `#include "module.h"`
  - Private static helpers and state
  - The implementation

Do not put function bodies in headers unless an inlined tiny static helper is
truly necessary and clearly marked.

## 4. File Header

Every `.cpp` and `.h` file must begin with a real file header in this
style:

```cpp
//
// Describe exactly what this file does.
// Be specific about the module purpose, important design choices,
// and any hardware assumptions that matter.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//
```

Rules:

- Replace the placeholder text with a real description.
- The comment must describe the file, not just list its symbols.
- Keep lines readable on an 72-column terminal where practical.

## 5. Function Documentation

Every function declared in a public header must have a comment
immediately above its prototype.

Example:

```c
//
// Look up an application id by application name.
//
// Parameters:
//      name        - Application name, usually space-padded in GEM
//                    style.
//
// Returns:
//      Matching AES application id, or a negative value if not found.
//
// Notes:
//      This is commonly used for accessory and desktop integration.
//
// Sample call:
//      WORD shell_id = appl_find("DESKTOP ");
//
WORD appl_find(char *name);
```

Rules:

- Say what the function does.
- Mention parameters and the return value when needed.
- Mention side effects, limits, or hardware requirements when
  relevant.
- Document private static helpers too when their purpose is not
  obvious.

## 6. General Coding Rules

- Fix all warnings under `-Wall -Wextra -pedantic`.
- Prefer small, focused functions.
- Keep source files under about 500 lines when possible.
- Avoid global variables unless they are truly necessary.
- Keep code readable on an 72-column terminal.
- Use 4 spaces for indentation and no tabs.
- Use K&R braces for functions. Control-flow brace style may vary
  within reason, but keep it consistent inside a file.

## 7. Build Rules

- Always build and test with G++ first.
- Use `-g -fsanitize=address,undefined` for hosted debug builds.
- All build outputs must go into the root `build/` directory only.
- Do not create `build/` directories inside `src/`, `lib/`, `tests/`,
  or any other subdirectory.
- Use GNU Make directly. No cmake, no meson, no generated build
  system. One handwritten `Makefile` per project, at the project root.
- The `Makefile` must discover sources under `src/` and `lib/` rather
  than listing them, so adding a file to the tree is enough to get it
  built.
- Object files and intermediate links go to
  `build/<project>/<build type>/`. Finished programs are copied to
  `bin/`.
- Every project supports exactly two build types, selected with
  `BUILD=`:
  - `debug`, the default: `-g -Og` with sanitizers.
  - `release`: `-O2 -DNDEBUG` with the C++ runtime linked in.
- Generate header dependencies with `-MMD -MP` and `-include` the
  resulting `.d` files, so editing a header rebuilds what uses it.
- Every project `Makefile` provides `all`, `test`, `clean`,
  `distclean`, and `help`.
- `distclean` removes what that project produced and nothing else.
  Because `bin/` is shared, delete the program by name rather than
  emptying the directory.
- In a multi-project repository, the top-level `Makefile` only
  orchestrates. It delegates to each project and defines no compile
  rules of its own.
- Keep VS Code `F5` debugging working through `.vscode/`.
- Link third-party libraries statically whenever the library license
  and build allow it. Every product ships as one self-contained
  executable.
- Link with `-static-libgcc -static-libstdc++` in release builds, and
  prefer the `.a` of a dependency over its `.so`.
- Do not use a fully static `-static` link on glibc. Hostname lookup
  goes through NSS modules that glibc loads at runtime, so a fully
  static binary silently fails to resolve anything. Everything but
  libc in the binary is the correct target here.
- If a dependency genuinely cannot be linked statically, such as a
  vendor SDK shipped only as a shared object, say so explicitly in the
  project `README.md`.

## 8. Documentation

- Keep `README.md` up to date.
- `README.md` must explain:
  - How to build the project
  - Dependencies
  - Usage
- Keep notes in `docs/` so the project retains context over time.

## 9. Tests

- Keep automated tests in `tests/`.
- Put test data files in `tests/data/`.
