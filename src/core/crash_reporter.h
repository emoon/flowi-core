#pragma once

#include "core.h"
#include "types.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Crash Reporter - Sentry/GlitchTip Integration (sentry-native SDK).
//
// When FL_ENABLE_SENTRY is not defined, all functions become no-ops and return stub values.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlArena;
struct CrashReporter;

typedef struct CrashReporter CrashReporter;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Configuration

typedef struct CrashReporterConfig {
    const char* dsn;           // Data Source Name (e.g., "http://key@host:port/project")
    const char* release;       // Version string (e.g., "replay-frontend@1.0.0")
    const char* environment;   // "production", "staging", "development"
    const char* database_path; // Path for crash cache (e.g., ".sentry-native")
    const char* handler_path;  // Path to crashpad_handler (nullptr = auto-detect)
} CrashReporterConfig;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lifecycle

// Returns stub instance if FL_ENABLE_SENTRY not defined
DLL_EXPORT CrashReporter* crash_reporter_create(struct FlArena* arena, CrashReporterConfig config);

// Shutdown crash reporter. Blocks up to 2 seconds to upload any pending crash data
DLL_EXPORT void crash_reporter_destroy(CrashReporter* reporter);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Status

// Returns true if FL_ENABLE_SENTRY was defined at compile time
DLL_EXPORT bool crash_reporter_is_available(void);

// Returns true if both compiled in AND enabled by user
DLL_EXPORT bool crash_reporter_is_enabled(CrashReporter* reporter);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Runtime Control

// Enable/disable at runtime (requires user consent/opt-in)
DLL_EXPORT void crash_reporter_set_enabled(CrashReporter* reporter, bool enabled);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Context

// user_id must be a persistent anonymous ID (not personally identifiable)
DLL_EXPORT void crash_reporter_set_user(CrashReporter* reporter, const char* user_id);

// Add key-value tag to all future crash reports
DLL_EXPORT void crash_reporter_set_tag(CrashReporter* reporter, const char* key, const char* value);

// Add breadcrumb to crash trail
DLL_EXPORT void crash_reporter_add_breadcrumb(CrashReporter* reporter, const char* message);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Testing

// DO NOT call in production - only for testing crash reporter setup
DLL_EXPORT void crash_reporter_trigger_test_crash(void);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
