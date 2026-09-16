#include "core.h"
#include "arena.h"
#include "arena_alloc_tracker.h"
#include "arena_watchdog.h"
#include "error_report.h"
#include "file_watcher.h"
#include "jobsys.h"
#include "log.h"
#include "perf_scope.h"
#include "sprintf.h"

#include <time.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Set once the process-wide services are up; cleared by fl_destroy.
static bool s_initialized = false;
static FlArena* s_main_arena = nullptr;

FlArena* fl_init(int job_threads) {
    if (s_initialized) {
        return s_main_arena;
    }

    arena_tracker_init(); // Must initialize tracker before creating any arenas
    arena_watchdog_init();
    arena_scratch_init();
    fl_perf_scope_init();
    error_report_init();

    // Cache the timezone before any thread starts: the C library's timezone functions are not
    // thread-safe and libarchive calls them internally when it reads timestamps.
#if PLATFORM_WINDOWS
    _tzset();
#else
    tzset();
#endif

    FlArena* main_arena = arena_new();
    fl_jobs_create(main_arena, job_threads); // This will skip if already initialized

    fl_log_set_level(LogLevel_Trace);

    // After the log level, so the watcher channel is registered at the level the rest of core gets.
    file_watcher_init();

    s_main_arena = main_arena;
    s_initialized = true;
    return main_arena;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef ARENA_TRACK_ALLOCATIONS
static void dump_arena_tracker(FlArena* arena, void* user_data) {
    (void)user_data;
    u64 pos = atomic_load_explicit(&arena->pos, memory_order_relaxed);
    if (pos > MB(1) && arena->alloc_tracker != nullptr) {
        arena_scratch_auto(temp);
        FlString name = sprintf_arena(temp.arena, "%s:%d", arena->allocation_site_file, arena->allocation_site_line);
        log_info("=== Arena \"%S\" — %.2f MB used ===", name, pos / (1024.0 * 1024.0));
        arena_alloc_tracker_dump_log(arena->alloc_tracker, string_to_cstr(temp.arena, name), 20);
    }
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_destroy(void) {
    if (!s_initialized) {
        return;
    }

#ifdef ARENA_TRACK_ALLOCATIONS
    log_info("=== Arena Allocation Tracker Dump (shutdown) ===");
    arena_tracker_iterate_all(dump_arena_tracker, nullptr);
#endif
    fl_perf_scope_destroy();
    error_report_destroy();
    fl_jobs_destroy();

    // After the jobs, so no worker can be inside a watcher call while the registry goes away.
    file_watcher_destroy();

    arena_watchdog_destroy();

    // The arenas unregister themselves as they go, so the tracker outlives every one of them.
    // Scratch goes last: logging allocates from it, so anything that logs after this would
    // resurrect it.
    arena_destroy(s_main_arena);
    arena_scratch_destroy();
    arena_tracker_destroy();

    s_main_arena = nullptr;
    s_initialized = false;
}
