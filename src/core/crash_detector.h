#pragma once

#include "core.h"
#include "types.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Crash Detector - Local crash state tracking for crash loop detection
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlArena;
struct FlString;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constants

#define CRASH_HISTORY_SIZE 10
#define CRASH_LOOP_THRESHOLD 3
#define CRASH_LOOP_WINDOW_SECONDS 60

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Types

typedef enum CrashPhase {
    CrashPhase_Unknown = 0,
    CrashPhase_Init,
    CrashPhase_PluginLoad,
    CrashPhase_Rendering,
    CrashPhase_Running,
} CrashPhase;

typedef enum CrashLoopStatus {
    CrashLoopStatus_None = 0,
    CrashLoopStatus_Detected,
    CrashLoopStatus_SafeModeActive
} CrashLoopStatus;

typedef struct CrashRecord {
    u64 timestamp;     // Unix timestamp (seconds since epoch)
    u32 exit_code;     // Exit code or signal number
    u32 startup_phase; // CrashPhase enum
} CrashRecord;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// API

// Override the data directory for crash state storage
// If not set, defaults to ~/<app-identity data dir> (neutral default ~/.flowi)
DLL_EXPORT void crash_detector_set_data_dir(struct FlString path);

// Returns CrashLoopStatus_Detected if 3+ crashes occurred in last 60 seconds
DLL_EXPORT CrashLoopStatus crash_detector_check(struct FlArena* arena);

// Record a crash from normal (non-signal) context, e.g. the crash reporter
DLL_EXPORT void crash_detector_record_crash(u32 exit_code, CrashPhase phase);

// Resolve and cache the crash-state file path (and create its directory) so that
// crash_detector_record_crash_from_signal can run without allocating or building paths.
// Must be called from normal context before signal handlers are installed.
// If the data dir or HOME changes afterwards, call again to re-resolve.
DLL_EXPORT void crash_detector_prepare_for_signal(void);

// Async-signal-safe variant of crash_detector_record_crash for use inside signal
// handlers. Uses only raw syscalls on the path cached by crash_detector_prepare_for_signal;
// silently does nothing if that preparation never succeeded.
DLL_EXPORT void crash_detector_record_crash_from_signal(u32 exit_code, CrashPhase phase);

DLL_EXPORT void crash_detector_record_successful_startup(void);

// Clear crash history on graceful shutdown (if uptime >= 60 seconds)
DLL_EXPORT void crash_detector_clear_on_graceful_shutdown(void);

DLL_EXPORT bool crash_detector_is_safe_mode_active(void);
DLL_EXPORT void crash_detector_exit_safe_mode(void);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
