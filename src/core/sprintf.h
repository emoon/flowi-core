#pragma once

#include "string.h"
#include <stdarg.h>

struct FlArena;
struct FlArenaMt;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// sprintf with SIMD vector support
//
// Every standard format specifier works, plus one specifier per simd.h vector type. Components print
// in bracket notation - floats with %.6g, integers with %d or %u - and an unrecognized vector
// specifier is emitted as literal text with the % dropped.
//
//   %v4f     f32x4     %v8i16   i16x8     %v16u8   u8x16
//   %v4i     i32x4     %v8u16   u16x8
//   %v4u     u32x4     %v8h     f16x8
//
//   f32x4 pos = f32x4_new(10.5f, 20.25f, -5.0f, 1.0f);
//   sprintf_arena(arena, "Position: %v4f", pos);   // "Position: [10.5, 20.25, -5, 1]"
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// sprintf_arena itself is declared FL_API in <flowi/string/string.h>, reached through string.h
// above; redeclaring it bare here clashes with its dllimport on clang-cl. The va_list and
// FlArenaMt entry points are not in the generated surface, so they stay here.
FlString vsprintf_arena(struct FlArena* arena, const char* format, va_list args);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Thread-safe sprintf - uses FlArenaMt for concurrent allocations
//
// These functions are identical to sprintf_arena/vsprintf_arena but use
// FlArenaMt for thread-safe allocation. Use these when formatting
// strings from multiple threads concurrently.
//
// Example:
//   FlArenaMt* arena_mt = arena_mt_new();
//   FlString msg = vsprintf_arena_mt(arena_mt, "Error: %S", error_path);
//   // Can be called safely from multiple threads

FlString sprintf_arena_mt(struct FlArenaMt* arena_mt, const char* format, ...);
FlString vsprintf_arena_mt(struct FlArenaMt* arena_mt, const char* format, va_list args);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
