#pragma once

// Public definition of FlString - the length-prefixed string flowi crosses by value
// through its public/plugin surface.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// FlString references either static or dynamic memory. length is the byte length
// (excluding any null terminator); is_static is 1 for static/literal storage;
// is_ascii is 1 when the bytes are known to be ASCII-only (0 = may be UTF-8).
typedef struct FlString {
    const char* data;
    uint64_t length : 62;
    uint64_t is_static : 1;
    uint64_t is_ascii : 1;
} FlString;

// Create an empty string (MSVC-compatible zero-init).
static inline FlString string_empty(void) {
    FlString result = { 0 };
    return result;
}

// Whether a string is empty (null data or zero length).
static inline bool string_is_empty(FlString str) {
    return str.data == 0 || str.length == 0;
}

static inline FlString string_from_cstr_len(const char* cstr, uint64_t len) {
    FlString result = { 0 };
    result.data = cstr;
    result.length = len;
    return result;
}

// Create a dynamic string view over a null-terminated C string. The bytes are
// not copied; the caller must keep cstr alive for the lifetime of the view.
static inline FlString string_from_cstr(const char* cstr) {
    return cstr ? string_from_cstr_len(cstr, (uint64_t)strlen(cstr)) : string_empty();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FlString Literal Macros
//
// Use S("literal") to create compile-time FlString constants from string literals.
// S() and S_() assume ASCII content (is_ascii = 1) for fast-path iteration.
// Use S_UTF8("literal") for string literals containing non-ASCII UTF-8 characters.

#define FL_STRING_ENSURE_STRING_LITERAL(x) ("" x "")
#define S(str)    \
    ((FlString) { \
        .data = FL_STRING_ENSURE_STRING_LITERAL(str), .length = sizeof(str) - 1, .is_static = 1, .is_ascii = 1 })

// This is used as a workaround for MSVC which doesn't allow you to initialize inside structs
#define S_(str) \
    { .data = FL_STRING_ENSURE_STRING_LITERAL(str), .length = sizeof(str) - 1, .is_static = 1, .is_ascii = 1 }

#define S_UTF8(str) \
    ((FlString) {   \
        .data = FL_STRING_ENSURE_STRING_LITERAL(str), .length = sizeof(str) - 1, .is_static = 1, .is_ascii = 0 })
