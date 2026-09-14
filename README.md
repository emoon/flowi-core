# flowi-core

The C foundation [flowi](https://github.com/emoon/flowi) is built on, and nothing above it:
an arena allocator, a work-stealing job system, channel logging, string and path handling,
JSON, a file watcher, hashing and a handful of OS shims. It owns no window, no renderer and
no filesystem abstraction.

It is a separate repository because more than one project needs exactly this much, and
because a process must contain exactly one copy of it: a host and the plugins it dlopens all
resolve `arena_*`, `string_*` and `heap_*` to the same symbols.

MIT. C11, no required dependency beyond zlib and libm.

## Building

```sh
cmake -S . -B build
cmake --build build
```

That produces a static `libflowi_core.a`. The options, all off by default:

| Option | Effect |
|---|---|
| `FLOWI_CORE_SHARED` | Build a shared `libflowi_core.so` instead |
| `FLOWI_CORE_USE_MIMALLOC` | Allocate through the bundled mimalloc rather than the C library |
| `FLOWI_CORE_ENABLE_PROFILING` | Emit Tracy zones from the `profile_*` macros |

An embedding project adds it with `add_subdirectory` and links `flowi_core`. Core's public
headers are `include/flowi/{arena,core,heap,path,string}` plus `fl_api.h`, spelled
`<flowi/core/jobs.h>` and so on — the same `flowi/` prefix flowi's own headers use, so a
generated header's cross-include resolves whichever side owns the type.

## Rust

`rust/` is its own cargo workspace with two crates:

- **`flowi_core_sys`** — the raw ABI. Generated bindings plus the hand-packed `RawStr`, the
  panic-containment guard, and the global allocator over core's heap. All the `unsafe`.
- **`flowi_core`** — the safe surface: a borrow-checked `Arena` whose allocations make
  `rewind` a borrow error, a closure-taking `Jobs`, the `log`-crate bridge, `path`, the file
  watcher, and the `boundary` kit for crates that hand-write extern "C" entry points.

By default `flowi_core_sys` builds the C library itself with cmake. A host whose own build
already links something containing core turns the `native` feature off, or sets one of
`FLOWI_CORE_SYS_SKIP_NATIVE_LINK`, `FLOWI_CORE_SYS_STATIC_LINK` or
`FLOWI_CORE_SYS_LIB_DIR`. Two copies of core in one process is the thing to avoid.

## Codegen

The public headers, the Rust bindings and the linker export lists are generated from the
IDL in `api/*.def` by [api-gen](https://github.com/emoon/api-gen), consumed as a library
from `codegen/` — a dev-only crate in its own workspace, so the generator's dependencies
never reach anything this repository ships. `cargo regen` regenerates; `cargo regen-check`
is the drift gate CI runs. Generated files are committed, so building needs no Rust
toolchain; only regenerating does.

A project layered on top of core names `api/` in api-gen's `reference_dirs`, so its own defs
can embed a core type by value without emitting a second copy of core's declarations.
