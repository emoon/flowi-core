#include "math.h"
#include "arena.h"
#include "simd.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IntersectResult f16x8_aabb_intersect_f32x4_fixed_array(FlArena* arena, const f16x8_soa_array* aabb_array,
                                                       f32x4 test_aabb) {
    // Allocate worst-case space for indices (all AABBs could intersect)
    const int max_aabbs = aabb_array->count * 8;
    u32* indices = arena_alloc_array(arena, u32, max_aabbs);
    int hit_count = 0;

    // test_aabb is [min_x, min_y, max_x, max_y]
    const f16x8 test_min_x = f16x8_broadcast(f32x4_extract(test_aabb, 0));
    const f16x8 test_min_y = f16x8_broadcast(f32x4_extract(test_aabb, 1));
    const f16x8 test_max_x = f16x8_broadcast(f32x4_extract(test_aabb, 2));
    const f16x8 test_max_y = f16x8_broadcast(f32x4_extract(test_aabb, 3));

    for (int i = 0, count = aabb_array->count; i < count; i++) {
        const f16x8 aabb_min_x = aabb_array->x[i];
        const f16x8 aabb_min_y = aabb_array->y[i];
        const f16x8 aabb_max_x = aabb_array->z[i];
        const f16x8 aabb_max_y = aabb_array->w[i];

        // AABB intersection test (all comparisons must be true):
        f16x8 cmp1 = f16x8_cmplt(test_min_x, aabb_max_x);
        f16x8 cmp2 = f16x8_cmplt(test_min_y, aabb_max_y);
        f16x8 cmp3 = f16x8_cmpgt(test_max_x, aabb_min_x);
        f16x8 cmp4 = f16x8_cmpgt(test_max_y, aabb_min_y);

        const f16x8 result = f16x8_and(f16x8_and(cmp1, cmp2), f16x8_and(cmp3, cmp4));

        int mask = f16x8_movemask(result);

        const int base_index = i * 8;

        while (mask != 0) {
            int bit_index = count_trailing_zeros(mask);
            indices[hit_count++] = base_index + bit_index;
            mask &= mask - 1; // Clear the lowest set bit
        }
    }

    arena_compact(arena, int, max_aabbs - hit_count);

    return (IntersectResult) { .indices = indices, .count = hit_count };
}
