#pragma once

#include "types.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ArenaAllocTracker ArenaAllocTracker;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ArenaAllocEntry {
    const char* file;
    int line;
    u64 total_bytes;
    u64 alloc_count;
} ArenaAllocEntry;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Uses mi_malloc to avoid a circular dependency with arenas.
ArenaAllocTracker* arena_alloc_tracker_create(void);

void arena_alloc_tracker_destroy(ArenaAllocTracker* tracker);

// Thread-safe.
void arena_alloc_tracker_record(ArenaAllocTracker* tracker, const char* file, int line, u64 size);

void arena_alloc_tracker_reset(ArenaAllocTracker* tracker);

// Writes the top callsites by total bytes (descending) into an array allocated from output_arena,
// and returns how many entries were written.
int arena_alloc_tracker_get_top(ArenaAllocTracker* tracker, struct FlArena* output_arena, int max_entries,
                                ArenaAllocEntry** out_entries);

void arena_alloc_tracker_dump_log(ArenaAllocTracker* tracker, const char* arena_name, int max_entries);

// Dumps every live arena with usage above 1 MB. Only available when ARENA_TRACK_ALLOCATIONS is defined.
void arena_alloc_tracker_dump_all(int max_entries_per_arena);
