#include "string_allocator.h"
#include "assert.h"
#include "math.h"
#include "memory.h"
#include "sprintf.h"
#include <stdarg.h>
#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u32 get_bucket_for_size(u64 size, u32 min_log2) {
    if (size == 0) {
        return 0;
    }

    // ceil(log2(size)): clz(size - 1) locates the highest bit of the next power of two.
    u32 log2 = 64 - clz_u64(size - 1);

    if (log2 < min_log2) {
        log2 = min_log2;
    }

    return log2 - min_log2;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StringAllocator* string_allocator_new(FlArena* arena) {
    StringAllocator* self = arena_alloc_zero(arena, StringAllocator);

    self->arena = arena;
    self->min_size_log2 = STRING_ALLOCATOR_MIN_SIZE_LOG2;
    self->max_size_log2 = STRING_ALLOCATOR_MAX_SIZE_LOG2;

    for_count(i, STRING_ALLOCATOR_NUM_BUCKETS) {
        fixed_array_new(&self->free_lists[i], arena, STRING_ALLOCATOR_MAX_FREE_LIST_SIZE);
    }

    mutex_init(&self->lock);

    return self;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString string_allocator_alloc(StringAllocator* self, u64 size) {
    FL_ASSERT(self->arena != nullptr);

    u32 bucket = get_bucket_for_size(size, self->min_size_log2);

    if (bucket >= STRING_ALLOCATOR_NUM_BUCKETS) {
        // Fall back to direct arena allocation for huge strings (arena is thread-safe)
        void* ptr = arena_alloc_raw(self->arena, size, 8);
        return (FlString) { .data = ptr, .length = size };
    }

    void* ptr = nullptr;
    u64 alloc_size = 1ULL << (bucket + self->min_size_log2);

    mutex_lock(&self->lock);

    if (self->free_lists[bucket].base.length > 0) {
        void** items = (void**)self->free_lists[bucket].base.data;
        ptr = items[--self->free_lists[bucket].base.length];

        asan_unpoison_memory_region(ptr, alloc_size);
    }

    mutex_unlock(&self->lock);

    // Allocate new chunk from arena outside lock (arena is thread-safe)
    if (ptr == nullptr) {
        ptr = arena_alloc_raw(self->arena, alloc_size, 8);
    }

    // Poison any excess memory beyond what was requested to detect overflows
    if (size < alloc_size) {
        asan_poison_memory_region((u8*)ptr + size, alloc_size - size);
    }

    return (FlString) { .data = ptr, .length = size };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void string_allocator_free(StringAllocator* self, FlString str) {

    // Static strings were not allocated here
    if (!str.data || str.length == 0 || str.is_static) {
        return;
    }

    u32 bucket = get_bucket_for_size(str.length, self->min_size_log2);

    // Don't free huge allocations (they're direct arena allocs)
    if (bucket >= STRING_ALLOCATOR_NUM_BUCKETS) {
        return;
    }

    u64 alloc_size = 1ULL << (bucket + self->min_size_log2);

    if (str.length < alloc_size) {
        asan_unpoison_memory_region((u8*)str.data + str.length, alloc_size - str.length);
    }

    // Poison the entire block to detect use-after-free
    asan_poison_memory_region(str.data, alloc_size);

    mutex_lock(&self->lock);

    if (self->free_lists[bucket].base.length >= STRING_ALLOCATOR_MAX_FREE_LIST_SIZE) {
        // Free list is full, just abandon the memory (stays in arena)
        mutex_unlock(&self->lock);
        return;
    }

    fixed_array_add(&self->free_lists[bucket], (void*)str.data);

    mutex_unlock(&self->lock);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void string_allocator_clear_free_lists(StringAllocator* self) {

    mutex_lock(&self->lock);

    for_count(i, STRING_ALLOCATOR_NUM_BUCKETS) {
        u64 alloc_size = 1ULL << (i + self->min_size_log2);
        void** items = (void**)self->free_lists[i].base.data;
        u64 count = self->free_lists[i].base.length;

        for_count(j, count) {
            asan_poison_memory_region(items[j], alloc_size);
        }

        self->free_lists[i].base.length = 0;
    }

    mutex_unlock(&self->lock);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString string_allocator_copy(StringAllocator* self, FlString src) {
    // A static string needs no allocation
    if (src.is_static || src.length == 0) {
        return src;
    }

    FlString dest = string_allocator_alloc(self, src.length);
    memory_copy((void*)dest.data, src.length, src.data, src.length);
    return dest;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString string_allocator_format(StringAllocator* self, const char* format, ...) {
    FL_ASSERT(format != nullptr);

    va_list args;
    va_start(args, format);
    FlString result = string_allocator_vformat(self, format, args);
    va_end(args);

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString string_allocator_vformat(StringAllocator* self, const char* format, va_list args) {
    FL_ASSERT(format != nullptr);

    arena_scratch_auto(temp);
    FlString formatted = vsprintf_arena(temp.arena, format, args);

    return string_allocator_copy(self, formatted);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

char* string_allocator_cstr(StringAllocator* self, FlString str) {
    FlString buffer = string_allocator_alloc(self, str.length + 1);

    if (str.length > 0) {
        memory_copy((void*)buffer.data, str.length, str.data, str.length);
    }

    ((char*)buffer.data)[str.length] = '\0';

    return (char*)buffer.data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
