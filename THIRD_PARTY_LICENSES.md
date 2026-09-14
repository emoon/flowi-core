# Third-party licenses

flowi-core itself is MIT (see [LICENSE](LICENSE)). The three libraries vendored under
`external/` keep their own licenses, reproduced in the files listed below. All three permit
redistribution in source and binary form provided their notices are retained.

| Library | License | Copyright | License text |
|---|---|---|---|
| [fzy](https://github.com/jhawthorn/fzy) | MIT | 2014 John Hawthorn | `external/fzy/LICENSE` |
| [mimalloc](https://github.com/microsoft/mimalloc) | MIT | 2018-2025 Microsoft Corporation, Daan Leijen | `external/mimalloc/LICENSE` |
| [xxHash](https://github.com/Cyan4973/xxHash) | BSD 2-Clause | 2012-2021 Yann Collet | `external/xxhash/LICENSE` |

mimalloc is built only when `FLOWI_CORE_USE_MIMALLOC` is on; xxHash and fzy always are.

fzy is a partial vendor — only `match.c`, `match.h`, `bonus.h` and `config*.h`, with a
hand-written `CMakeLists.txt` alongside and two portability edits (`<strings.h>` guarded
for Windows, flattened `config.h` include path); its MIT license permits this. The other
two are upstream copies.

zlib is a system dependency, found with `find_package(ZLIB)` rather than vendored, so its
notice travels with whatever copy the platform provides.
