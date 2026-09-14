#pragma once

#include "arena.h"
#include "string.h"
#include "fixed_array.h"
#include "os/os.h"
#include "types.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FlString Allocator with Free List Support
//
// Arena-backed, with a free list per power-of-2 size class.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define STRING_ALLOCATOR_NUM_BUCKETS 16
#define STRING_ALLOCATOR_MIN_SIZE_LOG2 3  // 8 bytes minimum
#define STRING_ALLOCATOR_MAX_SIZE_LOG2 18 // 256KB maximum
#define STRING_ALLOCATOR_MAX_FREE_LIST_SIZE 256

typedef struct StringAllocator {
    FlArena* arena;
    fixed_array(void*) free_lists[STRING_ALLOCATOR_NUM_BUCKETS];
    u32 min_size_log2;
    u32 max_size_log2;
    Mutex lock; // Thread-safety for free list operations
} StringAllocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core API

StringAllocator* string_allocator_new(FlArena* arena);
FlString string_allocator_alloc(StringAllocator* self, u64 size);
void string_allocator_free(StringAllocator* self, FlString str);

/// Clear all free lists (memory remains in arena)
void string_allocator_clear_free_lists(StringAllocator* self);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper functions

FlString string_allocator_copy(StringAllocator* self, FlString src);
FlString string_allocator_format(StringAllocator* self, const char* format, ...);
FlString string_allocator_vformat(StringAllocator* self, const char* format, va_list args);
char* string_allocator_cstr(StringAllocator* self, FlString str);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////