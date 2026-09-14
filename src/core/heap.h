#pragma once

#include "types.h"

#include <flowi/heap/heap.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Malloc-family heap allocator, backed by mimalloc (or the system allocator when sanitizers are on or
// on Windows).
//
// This is not an arena: allocations are individually owned and must be released with heap_free (or
// heap_free_aligned for heap_alloc_aligned). Use <core/arena.h> for bulk allocations freed together.

// The declarations themselves come from <flowi/heap/heap.h>, included above. They carry FL_API,
// which is __declspec(dllimport) wherever FL_API_IMPLEMENTATION is not defined, so redeclaring
// them bare here gave the same name two linkages and clang-cl rejects that.
