//
// The one translation unit that compiles stb_image.
//
// stb_image is Sean Barrett's single-header image loader, vendored
// unmodified in stb_image.h at v2.30 (commit f58f558c), and dual
// licensed MIT or public domain; see the end of that file. Only the
// PNG and JPEG decoders are built, since those are the only formats
// Evangelist captures, and only from memory, since a frame never
// touches the disk.
//
// Its warnings are its own, so they are silenced here and only here.
// Apostle's code is still compiled with every warning on.
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#include "stb_image.h"
#pragma GCC diagnostic pop
