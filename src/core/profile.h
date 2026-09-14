#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tracy profiler integration. Every macro below is a no-op unless ENABLE_PROFILING is defined.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "core.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Configuration Detection

#if defined(ENABLE_PROFILING) && ENABLE_PROFILING
#define PROFILE_ENABLED 1
#else
#define PROFILE_ENABLED 0
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tracy Integration

#if PROFILE_ENABLED

#include "string.h"
#include <TracyC.h>

// Core profiling macros - C API
#define profile_zone_scoped()        \
    TracyCZoneCtx ___tracy_zone_ctx; \
    TracyCZone(___tracy_zone_ctx, 1)
#define profile_zone_named(str)       \
    TracyCZoneCtx ___tracy_zone_ctx;  \
    TracyCZone(___tracy_zone_ctx, 1); \
    TracyCZoneName(___tracy_zone_ctx, (str).data, (str).length)
#define profile_zone_scoped_n(name)  \
    TracyCZoneCtx ___tracy_zone_ctx; \
    TracyCZoneN(___tracy_zone_ctx, name, 1)
#define profile_zone_text(str) TracyCZoneText(___tracy_zone_ctx, (str).data, (str).length)
#define profile_zone_value(value) TracyCZoneValue(___tracy_zone_ctx, value)
#define profile_zone_color(color) TracyCZoneColor(___tracy_zone_ctx, color)
#define profile_zone_end() TracyCZoneEnd(___tracy_zone_ctx)

// Function profiling - C API (requires manual profile_function_end)
// TracyCZone* macros declare the context variable, don't declare it separately
#define profile_function() TracyCZone(___tracy_function_ctx, 1)
#define profile_function_n(name) TracyCZoneN(___tracy_function_ctx, name, 1)
#define profile_function_nc(name, color) TracyCZoneNC(___tracy_function_ctx, name, color, 1)
#define profile_function_end() TracyCZoneEnd(___tracy_function_ctx)

// Frame marking
#define profile_frame_mark() FrameMark
#define profile_frame_mark_named(name) FrameMarkNamed(name)
#define profile_frame_mark_start(name) FrameMarkStart(name)
#define profile_frame_mark_end(name) FrameMarkEnd(name)

// Memory profiling
#define profile_alloc(ptr, size) TracyAlloc(ptr, size)
#define profile_free(ptr) TracyFree(ptr)
#define profile_secure_alloc(ptr, size) TracySecureAlloc(ptr, size)
#define profile_secure_free(ptr) TracySecureFree(ptr)

// Message logging (FlString versions)
#define profile_message(str) TracyMessage((str).data, (str).length)
#define profile_message_c(str, color) TracyMessageC((str).data, (str).length, color)

// Application info (FlString version)
#define profile_app_info(str) TracyAppInfo((str).data, (str).length)

// Plotting
#define profile_plot(name, value) TracyPlot(name, value)

// Lock profiling
#define profile_lockable(type, varname) TracyLockable(type, varname)
#define profile_lockable_n(type, varname, desc) TracyLockableN(type, varname, desc)
#define profile_shared_lockable(type, varname) TracySharedLockable(type, varname)
#define profile_shared_lockable_n(type, varname, desc) TracySharedLockableN(type, varname, desc)

// Fiber support
#define profile_fiber_enter(fiber) TracyFiberEnter(fiber)
#define profile_fiber_leave() TracyFiberLeave()

// Custom colors (RGB)
#define PROFILE_COLOR_RED 0xFF0000
#define PROFILE_COLOR_GREEN 0x00FF00
#define PROFILE_COLOR_BLUE 0x0000FF
#define PROFILE_COLOR_YELLOW 0xFFFF00
#define PROFILE_COLOR_MAGENTA 0xFF00FF
#define PROFILE_COLOR_CYAN 0x00FFFF
#define PROFILE_COLOR_WHITE 0xFFFFFF
#define PROFILE_COLOR_ORANGE 0xFF8000
#define PROFILE_COLOR_PINK 0xFF8080

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Profiling disabled - all macros become no-ops

#else

// Core profiling macros
#define profile_zone_scoped()
#define profile_zone_named(str)
#define profile_zone_scoped_n(name)
#define profile_zone_text(str)
#define profile_zone_value(value)
#define profile_zone_color(color)
#define profile_zone_end()

// Function profiling
#define profile_function()
#define profile_function_n(name)
#define profile_function_nc(name, color)
#define profile_function_end()

// Frame marking
#define profile_frame_mark()
#define profile_frame_mark_named(name)
#define profile_frame_mark_start(name)
#define profile_frame_mark_end(name)

// Memory profiling
#define profile_alloc(ptr, size)
#define profile_free(ptr)
#define profile_secure_alloc(ptr, size)
#define profile_secure_free(ptr)

// Message logging (FlString versions)
#define profile_message(str)
#define profile_message_c(str, color)

// Application info (FlString version)
#define profile_app_info(str)

// Plotting
#define profile_plot(name, value)

// Lock profiling
#define profile_lockable(type, varname) type varname
#define profile_lockable_n(type, varname, desc) type varname
#define profile_shared_lockable(type, varname) type varname
#define profile_shared_lockable_n(type, varname, desc) type varname

// Fiber support
#define profile_fiber_enter(fiber)
#define profile_fiber_leave()

// Custom colors (unused when disabled)
#define PROFILE_COLOR_RED 0
#define PROFILE_COLOR_GREEN 0
#define PROFILE_COLOR_BLUE 0
#define PROFILE_COLOR_YELLOW 0
#define PROFILE_COLOR_MAGENTA 0
#define PROFILE_COLOR_CYAN 0
#define PROFILE_COLOR_WHITE 0
#define PROFILE_COLOR_ORANGE 0
#define PROFILE_COLOR_PINK 0

#endif // PROFILE_ENABLED

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Automatic Cleanup Support (GCC/Clang)

#if PROFILE_ENABLED && (COMPILER_CLANG || COMPILER_GCC)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void tracy_zone_cleanup(TracyCZoneCtx* ctx) {
    if (ctx && ctx->active) {
        TracyCZoneEnd(*ctx);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Automatic cleanup macros for function profiling

#define profile_function_auto()                                                       \
    __attribute__((cleanup(tracy_zone_cleanup))) TracyCZoneCtx ___tracy_function_ctx; \
    TracyCZone(___tracy_function_ctx, 1)

#define profile_function_auto_n(name)                                                 \
    __attribute__((cleanup(tracy_zone_cleanup))) TracyCZoneCtx ___tracy_function_ctx; \
    TracyCZoneN(___tracy_function_ctx, name, 1)

#define profile_function_auto_c(color)                                                \
    __attribute__((cleanup(tracy_zone_cleanup))) TracyCZoneCtx ___tracy_function_ctx; \
    TracyCZone(___tracy_function_ctx, 1);                                             \
    TracyCZoneColor(___tracy_function_ctx, color)

#define profile_function_auto_nc(name, color)                                         \
    __attribute__((cleanup(tracy_zone_cleanup))) TracyCZoneCtx ___tracy_function_ctx; \
    TracyCZoneNC(___tracy_function_ctx, name, color, 1)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Automatic cleanup macros for zone profiling

#define profile_zone_auto()                                                       \
    __attribute__((cleanup(tracy_zone_cleanup))) TracyCZoneCtx ___tracy_zone_ctx; \
    TracyCZone(___tracy_zone_ctx, 1)

#define profile_zone_auto_n(name)                                                 \
    __attribute__((cleanup(tracy_zone_cleanup))) TracyCZoneCtx ___tracy_zone_ctx; \
    TracyCZoneN(___tracy_zone_ctx, name, 1)

#define profile_zone_auto_c(color)                                                \
    __attribute__((cleanup(tracy_zone_cleanup))) TracyCZoneCtx ___tracy_zone_ctx; \
    TracyCZone(___tracy_zone_ctx, 1);                                             \
    TracyCZoneColor(___tracy_zone_ctx, color)

#define profile_zone_auto_nc(name, color)                                         \
    __attribute__((cleanup(tracy_zone_cleanup))) TracyCZoneCtx ___tracy_zone_ctx; \
    TracyCZoneNC(___tracy_zone_ctx, name, color, 1)

#else
// Profiling disabled or compiler does not support cleanup attribute

#define profile_function_auto()
#define profile_function_auto_n(name)
#define profile_function_auto_c(color)
#define profile_function_auto_nc(name, color)

#define profile_zone_auto()
#define profile_zone_auto_n(name)
#define profile_zone_auto_c(color)
#define profile_zone_auto_nc(name, color)

#endif // PROFILE_ENABLED && (COMPILER_CLANG || COMPILER_GCC)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convenience macros for common patterns

#if PROFILE_ENABLED
#define profile_scope() profile_function()
#else
#define profile_scope()
#endif

#define profile_scope_n(name) profile_zone_scoped_n(name)

// Profile with color (requires manual profile_zone_end)
#define profile_scope_c(color) \
    profile_zone_scoped();     \
    profile_zone_color(color)

// Profile with name and color (requires manual profile_zone_end)
#define profile_scope_nc(name, color) \
    profile_zone_scoped_n(name);      \
    profile_zone_color(color)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
