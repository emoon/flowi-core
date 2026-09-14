#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UTF-8 decoding utilities
//
// Core decoder is Christopher Wellons' branchless UTF-8 decoder (public domain):
// https://github.com/skeeto/branchless-utf8
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "string.h"
#include "types.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constants

// Unicode replacement character (U+FFFD) - used for invalid UTF-8 sequences
#define UTF8_REPLACEMENT_CHAR 0xFFFD

// Maximum valid Unicode codepoint
#define UTF8_MAX_CODEPOINT 0x10FFFF

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Branchless UTF-8 decoder by Christopher Wellons
// https://github.com/skeeto/branchless-utf8
// This is free and unencumbered software released into the public domain.
//
// IMPORTANT: Buffer must have at least 3 bytes of zero-padding after valid data
// for safe read-ahead. Use utf8_decode_safe() if you can't guarantee padding.
//
// Errors are reported in *e, which will be non-zero if the parsed character was
// invalid: invalid byte sequence, non-canonical encoding, or a surrogate half.
//
// Returns pointer to next character (always advances at least one byte).

static inline void* utf8_decode(void* buf, u32* c, int* e) {
    // clang-format off
    static const char lengths[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0
    };
    static const int masks[]  = {0x00, 0x7f, 0x1f, 0x0f, 0x07};
    static const u32 mins[] = {4194304, 0, 128, 2048, 65536};
    static const int shiftc[] = {0, 18, 12, 6, 0};
    static const int shifte[] = {0, 6, 4, 2, 0};
    // clang-format on

    u8* s = buf;
    int len = lengths[s[0] >> 3];

    // Compute next pointer early for better pipelining
    u8* next = s + len + !len;

    // Load four bytes, shift out unused bits
    *c = (u32)(s[0] & masks[len]) << 18;
    *c |= (u32)(s[1] & 0x3f) << 12;
    *c |= (u32)(s[2] & 0x3f) << 6;
    *c |= (u32)(s[3] & 0x3f) << 0;
    *c >>= shiftc[len];

    // Accumulate error conditions
    *e = (*c < mins[len]) << 6;      // non-canonical encoding
    *e |= ((*c >> 11) == 0x1b) << 7; // surrogate half?
    *e |= (*c > 0x10FFFF) << 8;      // out of range?
    *e |= (s[1] & 0xc0) >> 2;
    *e |= (s[2] & 0xc0) >> 4;
    *e |= (s[3]) >> 6;
    *e ^= 0x2a; // top two bits of each tail byte correct?
    *e >>= shifte[len];

    return next;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Safe decoder with bounds checking (for FlString without zero-padding)

// Decode next codepoint with bounds checking.
// Returns nullptr and sets *codepoint to 0 when end is reached.
// Invalid sequences produce UTF8_REPLACEMENT_CHAR.
const char* utf8_decode_safe(const char* ptr, const char* end, u32* codepoint);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UTF-8 string iteration helper

typedef struct Utf8Iter {
    const char* ptr; // Current position
    const char* end; // End of string
} Utf8Iter;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline Utf8Iter utf8_iter(FlString str) {
    return (Utf8Iter) { .ptr = str.data, .end = str.data + str.length };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline bool utf8_iter_done(const Utf8Iter* it) {
    return it->ptr >= it->end;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u32 utf8_iter_next(Utf8Iter* it) {
    if (it->ptr >= it->end) {
        return 0;
    }
    u32 cp;
    it->ptr = utf8_decode_safe(it->ptr, it->end, &cp);
    return cp;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions

// Count codepoints in UTF-8 string (O(n))
u64 utf8_codepoint_count(FlString str);

// Encode codepoint to UTF-8. Buffer must have 4 bytes available.
// Returns bytes written (1-4), or 0 for invalid codepoints.
u32 utf8_encode(char* buf, u32 codepoint);

// Validate UTF-8 string
bool utf8_is_valid(FlString str);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline bool utf8_is_continuation(u8 byte) {
    return (byte & 0xC0) == 0x80;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline u32 utf8_sequence_length(u8 first_byte) {
    if (first_byte < 0x80)
        return 1;
    if (first_byte >= 0xC2 && first_byte <= 0xDF)
        return 2;
    if ((first_byte & 0xF0) == 0xE0)
        return 3;
    if (first_byte >= 0xF0 && first_byte <= 0xF4)
        return 4;
    return 0;
}
