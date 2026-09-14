# src/core/ - Core Library

Foundation layer providing platform-independent primitives. All other modules depend on this.

**Types use the `Fl` prefix** (`FlArena`, `FlString`). Internal functions are unprefixed
(`arena_new`, `string_copy`); `fl_` is reserved for the generated public API and the handful of
hand-written entry points that join it (`fl_init`, `fl_destroy`, `fl_log_*`, `fl_jobs_*`).

## Key Modules

| Module | Files | Purpose |
|--------|-------|---------|
| Arena | `arena.h/c`, `arena_typed.h` | Primary allocator (virtual-memory bump allocator) |
| Strings | `string.h/c`, `sprintf.h/c`, `utf8.h/c` | `FlString` operations, formatting, UTF-8 |
| Collections | `hashmap.h/c`, `fixed_array.h/c`, `array.h` | Type-safe containers, all arena-backed |
| OS | `os/os.h` | File I/O, threads, mutexes, virtual memory, timing |
| Jobs | `jobsys.h/c` | Work-stealing job system with priorities and dependencies |
| Logging | `log.h/c` | Channel-based logger. Use `%S` for FlString, `%s` for char* |
| Assertions | `assert.h/c` | `assert()`, `FL_ASSERT`, `FL_VALIDATE` macros |
| JSON | `json.h/c`, `json_builder.h/c` | Arena-allocated DOM parser and builder |
| Path | `path.h/c` | Cross-platform path operations |
| Hash | `hash.h/c` | CRC32, SHA1, MD5 |
| Math/SIMD | `math.h`, `simd.h/c` | Alignment, clamping, SIMD abstractions (SSE/NEON) |
| Profiling | `perf_scope.h/c`, `profile.h` | Lightweight frame profiler, optional Tracy integration |

## Arena Patterns

```c
// Permanent allocation
MyData* data = arena_alloc_zero(arena, MyData);

// Temporary scratch with auto-cleanup (preferred)
arena_scratch_auto(temp);
char* buf = arena_alloc_array(temp.arena, char, 1024);
// No cleanup needed - automatic at scope exit

// Temp view into existing arena (auto-rewinds)
arena_temp_auto(temp, ctx->arena);

// Typed append-only array on an arena
arena_typed(MyEntry) entries;
arena_typed_init(&entries, arena);
```

**Pitfall - scratch aliasing:** If a function receives `FlArena* arena`, use `arena_scratch_auto_conflict(temp, arena)` to avoid getting the same arena back.

## String Patterns

```c
FlString path = S("/home/user/file.txt");           // Static literal
FlString copy = string_copy(arena, path);            // Arena copy
FlString msg = sprintf_arena(arena, "err: %S", p);   // Formatted
FlStringBuilder sb = sb_create(arena);               // Builder
sb = sb_appendf(sb, "prefix_%S", name);
FlString result = sb_to_string(sb);
```

## Error Handling

- **Public API:** `FL_VALIDATE(expr, return_val)` - aborts in debug, logs + returns in release
- **Internal static functions:** `assert(expr)` - debug only
- **Runtime errors:** Return result structs with `FlStatus`
- **No silent failures** - every error must log or return error status

## OS Abstraction (`os/os.h`)

**Never use POSIX/Win32 APIs directly.** Use:
- Files: `file_read_os()`, `file_write_os()`, `file_open_os()`, `file_mmap_open_os()`
- Dirs: `dir_open_os()`, `dir_read_next_os()`, `file_create_directory_recursive_os()`
- Threads: `os_thread_create()`, `os_thread_join()`
- Sync: `mutex_init()`, `mutex_lock_auto(&m)` (RAII), `condvar_*`
- Time: `sleep_us()`, `sleep_ms()`, `get_monotonic_time_ns_os()`
- VM: `reserve_range_os()`, `commit_memory_os()`

## Common Pitfalls

1. **`%S` vs `%s`** - Using `%s` with FlString is undefined behavior (struct, not null-terminated)
2. **`fixed_array` is fixed capacity** - does NOT grow; `fixed_array_add` silently returns `{0}` when full
3. **`hashmap` has no destroy** - memory reclaimed when arena rewinds/destroys
4. **`jobs_wait` blocks** - use `jobs_is_finished()` polling on main thread instead
5. **`arena_alloc` is thread-safe** (CAS) - use `_st` suffix only for provably single-threaded paths
6. **`mutex_lock_auto(&m)`** is RAII - prefer over manual lock/unlock pairs

## Do Not Modify

- `tlsf.h` / `tlsf.c` - third-party allocator

## Initialization

`fl_init(job_threads)` brings up arenas, jobs and logging, and must run before anything else
touches core. It is idempotent — a second call returns the live main arena — and `fl_app_create`
calls it, so only a host that wants a specific `job_threads` count, or that needs the arena before
there is an application, calls it directly. `fl_destroy()` must be called last.
