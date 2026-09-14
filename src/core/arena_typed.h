#pragma once

#include "arena.h"
#include "core.h"
#include "types.h"
#include <assert.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Type-safe arena implementation
//
// Usage:
//   arena_typed(i32) my_i32_arena;
//   arena_typed_init(&my_i32_arena, arena);
//   i32* value = arena_typed_alloc(&my_i32_arena);
//   i32* zero_value = arena_typed_alloc_zero(&my_i32_arena);
//   i32* array = arena_typed_alloc_fixed_array(&my_i32_arena, 10);
//   i32* zero_array = arena_typed_alloc_array_zero(&my_i32_arena, 10);
//   u64 count = arena_typed_count(&my_i32_arena);
//   arena_typed_rewind(&my_i32_arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define arena_typed(element_type)    \
    struct {                         \
        FlArena* arena;              \
        void* start_pos;             \
        bool owns_arena;             \
        element_type* _element_type; \
    }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Creates and owns its own arena

#define arena_typed_new(typed_arena_ptr)                                            \
    do {                                                                            \
        (typed_arena_ptr)->arena = arena_new();                                     \
        (typed_arena_ptr)->start_pos = arena_current_pos((typed_arena_ptr)->arena); \
        (typed_arena_ptr)->owns_arena = true;                                       \
        (typed_arena_ptr)->_element_type = nullptr;                                 \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Borrows an existing arena

#define arena_typed_init(typed_arena_ptr, arena_ptr)                 \
    do {                                                             \
        (typed_arena_ptr)->arena = (arena_ptr);                      \
        (typed_arena_ptr)->start_pos = arena_current_pos(arena_ptr); \
        (typed_arena_ptr)->owns_arena = false;                       \
        (typed_arena_ptr)->_element_type = nullptr;                  \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define arena_typed_alloc(typed_arena_ptr)                                                                \
    ({                                                                                                    \
        typedef __typeof__(*(typed_arena_ptr)->_element_type) _element_t;                                 \
        (_element_t*)arena_alloc_raw((typed_arena_ptr)->arena, sizeof(_element_t), align_of(_element_t)); \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define arena_typed_alloc_zero(typed_arena_ptr)                                                                \
    ({                                                                                                         \
        typedef __typeof__(*(typed_arena_ptr)->_element_type) _element_t;                                      \
        (_element_t*)arena_alloc_raw_zero((typed_arena_ptr)->arena, sizeof(_element_t), align_of(_element_t)); \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define arena_typed_alloc_fixed_array(typed_arena_ptr, count)                                                       \
    ({                                                                                                              \
        typedef __typeof__(*(typed_arena_ptr)->_element_type) _element_t;                                           \
        (_element_t*)arena_alloc_raw((typed_arena_ptr)->arena, (count) * sizeof(_element_t), align_of(_element_t)); \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define arena_typed_alloc_array_zero(typed_arena_ptr, count)                                      \
    ({                                                                                            \
        typedef __typeof__(*(typed_arena_ptr)->_element_type) _element_t;                         \
        (_element_t*)arena_alloc_raw_zero((typed_arena_ptr)->arena, (count) * sizeof(_element_t), \
                                          align_of(_element_t));                                  \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define arena_typed_rewind(typed_arena_ptr) arena_rewind_to((typed_arena_ptr)->arena, (typed_arena_ptr)->start_pos)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Elements allocated since initialization

#define arena_typed_count(typed_arena_ptr)                                                                             \
    ({                                                                                                                 \
        typedef __typeof__(*(typed_arena_ptr)->_element_type) _element_t;                                              \
        u64 _bytes_used = (u64)((u8*)arena_current_pos((typed_arena_ptr)->arena) - (u8*)(typed_arena_ptr)->start_pos); \
        _bytes_used / sizeof(_element_t);                                                                              \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define arena_typed_data(typed_arena_ptr)                                 \
    ({                                                                    \
        typedef __typeof__(*(typed_arena_ptr)->_element_type) _element_t; \
        (_element_t*)(typed_arena_ptr)->start_pos;                        \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Alias for arena_typed_data

#define arena_typed_base_ptr(typed_arena_ptr) arena_typed_data(typed_arena_ptr)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Only destroys the underlying arena if owns_arena is true

#define arena_typed_destroy(typed_arena_ptr)                             \
    do {                                                                 \
        if ((typed_arena_ptr)->owns_arena && (typed_arena_ptr)->arena) { \
            arena_destroy((typed_arena_ptr)->arena);                     \
        }                                                                \
        (typed_arena_ptr)->arena = nullptr;                              \
        (typed_arena_ptr)->start_pos = nullptr;                          \
        (typed_arena_ptr)->owns_arena = false;                           \
        (typed_arena_ptr)->_element_type = nullptr;                      \
    } while (0)