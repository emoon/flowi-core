#pragma once

#include "core.h"
#include "string.h"
#include "types.h"

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Performance counters - hardware performance monitoring
//
// Most CPUs can only monitor 4-8 counters at once; a session is one such counter configuration.
// Linux reads them through perf_event_open; Windows and macOS are API-compatible stubs.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Configuration Detection

#ifndef PERF_COUNTERS_ENABLED
#if defined(ENABLE_PERF_COUNTERS) && ENABLE_PERF_COUNTERS
#define PERF_COUNTERS_ENABLED 1
#else
#define PERF_COUNTERS_ENABLED 0
#endif
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Platform-specific includes and definitions

#if PLATFORM_LINUX
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Performance Counter Event Types
//
// Standardized event types across all supported platforms. Platform backends
// map these to the appropriate hardware-specific event codes.

typedef enum PerfCounterEvent {
    PERF_COUNTER_CYCLES,
    PERF_COUNTER_INSTRUCTIONS,
    PERF_COUNTER_CACHE_L1D_MISS,
    PERF_COUNTER_CACHE_L1D_ACCESS,
    PERF_COUNTER_CACHE_L1I_MISS,
    PERF_COUNTER_CACHE_L1I_ACCESS,
    PERF_COUNTER_CACHE_L2_MISS,
    PERF_COUNTER_CACHE_L2_ACCESS,
    PERF_COUNTER_BRANCH_MISSES,
    PERF_COUNTER_BRANCH_INSTRUCTIONS,
    PERF_COUNTER_MEMORY_STORES,
    PERF_COUNTER_MEMORY_LOADS,
    PERF_COUNTER_TLB_MISS,
    PERF_COUNTER_STALL_FRONTEND,
    PERF_COUNTER_STALL_BACKEND,
    PERF_COUNTER_COUNT // Total number of supported events
} PerfCounterEvent;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core Types

// Matches hardware counter width
typedef u64 PerfCounterValue;

typedef struct PerfCounterConfig {
    PerfCounterEvent event;
    bool enabled;
    FlString name;
} PerfCounterConfig;

typedef struct PerfCounterSession {
    void* platform_data[8]; // Platform-specific session data (FDs, handles, etc.)
    PerfCounterConfig counters[8];
    u32 num_counters; // Number of active counters (1-8)
    bool is_active;
} PerfCounterSession;

typedef struct PerfCounterResult {
    PerfCounterValue values[8]; // Counter values (one per slot)
    u32 num_values;
    bool overflow;
} PerfCounterResult;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core API Functions

// Must be called before using any other functions.
// Returns false if counters are not available.
bool perf_counters_init(void);

void perf_counters_shutdown(void);
bool perf_counters_available(void);

PerfCounterSession* perf_counter_session_create(struct FlArena* arena);
void perf_counter_session_destroy(PerfCounterSession* session);

// Up to 8 counters can be configured simultaneously
bool perf_counter_session_configure(PerfCounterSession* session, const PerfCounterConfig* configs, u32 num_configs);

bool perf_counter_session_start(PerfCounterSession* session);
bool perf_counter_session_stop(PerfCounterSession* session, PerfCounterResult* result);

// Reset counters without stopping the session
bool perf_counter_session_reset(PerfCounterSession* session);

// Index corresponds to the order in the configuration array
static inline PerfCounterValue perf_counter_result_get(const PerfCounterResult* result, u32 index) {
    if (index < result->num_values) {
        return result->values[index];
    }
    return 0;
}

FlString perf_counter_session_get_name(const PerfCounterSession* session, u32 index);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Platform-specific backend functions
//
// Implemented in platform-specific files; call the core API above instead.
bool perf_counters_init_impl(void);
void perf_counters_shutdown_impl(void);
bool perf_counters_available_impl(void);
PerfCounterSession* perf_counter_session_create_impl(struct FlArena* arena);
void perf_counter_session_destroy_impl(PerfCounterSession* session);
bool perf_counter_session_configure_impl(PerfCounterSession* session, const PerfCounterConfig* configs,
                                         u32 num_configs);
bool perf_counter_session_start_impl(PerfCounterSession* session);
bool perf_counter_session_stop_impl(PerfCounterSession* session, PerfCounterResult* result);
bool perf_counter_session_reset_impl(PerfCounterSession* session);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions

// An event decoded into the two perf_event_attr fields that select it: type is a PERF_TYPE_*
// value and config the event code within that type (for PERF_TYPE_HW_CACHE it packs cache id,
// operation and result).
typedef struct PerfCounterPlatformEvent {
    u32 type;
    u64 config;
} PerfCounterPlatformEvent;

// Used internally by platform backends
PerfCounterPlatformEvent perf_counter_event_to_platform_event(PerfCounterEvent event);

FlString perf_counter_event_to_string(PerfCounterEvent event);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
