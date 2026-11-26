# apostle/lib/

Project libraries and third-party libraries, one directory each, as
`lib/<name>/`. Sources placed here are found and built automatically by
the project `Makefile`.

| Library | What | Licence |
| --- | --- | --- |
| `stb/` | `stb_image.h` v2.30 (nothings/stb, commit `f58f558c`), unmodified, compiled by `stb_image.cpp` for PNG and JPEG from memory only. Decodes captured pages so Apostle can reduce them for readers that cannot decode. | MIT or public domain, at your choice; see the end of `stb_image.h` |

Third-party code is linked statically. See section 7 of
`docs/standards/CPP-CODING-STYLE.md`.
