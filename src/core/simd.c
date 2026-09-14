#include "simd.h"
#include "arena.h"
#include "core.h"
#include "assert.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Transposes AoS→SoA with register shuffles, without memory round-trips.
// REQUIRES: count must be a multiple of 8
f16x8_soa_array f16x8_soa_from_f32x4_fixed_array(FlArena* arena, const f32x4* input, int count) {
    FL_ASSERT(count % 8 == 0 && "Input count must be multiple of 8");

    int batch_count = count / 8;

    f16x8_soa_array result;
    result.x = arena_alloc_array(arena, f16x8, batch_count);
    result.y = arena_alloc_array(arena, f16x8, batch_count);
    result.z = arena_alloc_array(arena, f16x8, batch_count);
    result.w = arena_alloc_array(arena, f16x8, batch_count);
    result.count = batch_count;

    for_count(batch, batch_count) {
        int start_idx = batch * 8;

#if CPU_ARM64
        // ARM64: Use deinterleaved loads to directly convert AoS→SoA
        const float* data = (const float*)&input[start_idx];

        float32x4x4_t lows = vld4q_f32(data);
        float32x4x4_t highs = vld4q_f32(data + 16);

        result.x[batch].v = vcombine_f16(vcvt_f16_f32(lows.val[0]), vcvt_f16_f32(highs.val[0]));
        result.y[batch].v = vcombine_f16(vcvt_f16_f32(lows.val[1]), vcvt_f16_f32(highs.val[1]));
        result.z[batch].v = vcombine_f16(vcvt_f16_f32(lows.val[2]), vcvt_f16_f32(highs.val[2]));
        result.w[batch].v = vcombine_f16(vcvt_f16_f32(lows.val[3]), vcvt_f16_f32(highs.val[3]));

#elif CPU_X64 || CPU_X86
        // x64: Use SSE shuffles to transpose 8x4 → 4x8 matrix
        __m128 v0 = input[start_idx + 0].v;
        __m128 v1 = input[start_idx + 1].v;
        __m128 v2 = input[start_idx + 2].v;
        __m128 v3 = input[start_idx + 3].v;
        __m128 v4 = input[start_idx + 4].v;
        __m128 v5 = input[start_idx + 5].v;
        __m128 v6 = input[start_idx + 6].v;
        __m128 v7 = input[start_idx + 7].v;

        __m128 tmp0 = _mm_unpacklo_ps(v0, v1); // [x0,x1,y0,y1]
        __m128 tmp1 = _mm_unpackhi_ps(v0, v1); // [z0,z1,w0,w1]
        __m128 tmp2 = _mm_unpacklo_ps(v2, v3); // [x2,x3,y2,y3]
        __m128 tmp3 = _mm_unpackhi_ps(v2, v3); // [z2,z3,w2,w3]
        __m128 tmp4 = _mm_unpacklo_ps(v4, v5); // [x4,x5,y4,y5]
        __m128 tmp5 = _mm_unpackhi_ps(v4, v5); // [z4,z5,w4,w5]
        __m128 tmp6 = _mm_unpacklo_ps(v6, v7); // [x6,x7,y6,y7]
        __m128 tmp7 = _mm_unpackhi_ps(v6, v7); // [z6,z7,w6,w7]

        __m128 x_low = _mm_shuffle_ps(tmp0, tmp2, _MM_SHUFFLE(1, 0, 1, 0)); // [x0,x1,x2,x3]
        __m128 y_low = _mm_shuffle_ps(tmp0, tmp2, _MM_SHUFFLE(3, 2, 3, 2)); // [y0,y1,y2,y3]
        __m128 z_low = _mm_shuffle_ps(tmp1, tmp3, _MM_SHUFFLE(1, 0, 1, 0)); // [z0,z1,z2,z3]
        __m128 w_low = _mm_shuffle_ps(tmp1, tmp3, _MM_SHUFFLE(3, 2, 3, 2)); // [w0,w1,w2,w3]

        __m128 x_high = _mm_shuffle_ps(tmp4, tmp6, _MM_SHUFFLE(1, 0, 1, 0)); // [x4,x5,x6,x7]
        __m128 y_high = _mm_shuffle_ps(tmp4, tmp6, _MM_SHUFFLE(3, 2, 3, 2)); // [y4,y5,y6,y7]
        __m128 z_high = _mm_shuffle_ps(tmp5, tmp7, _MM_SHUFFLE(1, 0, 1, 0)); // [z4,z5,z6,z7]
        __m128 w_high = _mm_shuffle_ps(tmp5, tmp7, _MM_SHUFFLE(3, 2, 3, 2)); // [w4,w5,w6,w7]

        // Store as f32x4 pairs (x64 has no native f16)
        result.x[batch].low = (f32x4) { .v = x_low };
        result.x[batch].high = (f32x4) { .v = x_high };
        result.y[batch].low = (f32x4) { .v = y_low };
        result.y[batch].high = (f32x4) { .v = y_high };
        result.z[batch].low = (f32x4) { .v = z_low };
        result.z[batch].high = (f32x4) { .v = z_high };
        result.w[batch].low = (f32x4) { .v = w_low };
        result.w[batch].high = (f32x4) { .v = w_high };
#else
#error "Unsupported architecture"
#endif
    }

    return result;
}
