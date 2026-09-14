#include "crash_reporter.h"
#include "arena.h"
#include "string.h"
#include "log.h"

#ifdef FL_ENABLE_SENTRY
#include <sentry.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct CrashReporter {
    bool enabled;
    bool initialized;
#ifdef FL_ENABLE_SENTRY
    sentry_options_t* options;
#endif
} CrashReporter;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CrashReporter* crash_reporter_create(FlArena* arena, CrashReporterConfig config) {
    CrashReporter* reporter = arena_alloc(arena, CrashReporter);

#ifndef FL_ENABLE_SENTRY
    (void)config;
    reporter->enabled = false;
    reporter->initialized = false;
    log_debug("Crash reporter: not available (compiled without FL_ENABLE_SENTRY)");
    return reporter;
#else
    if (config.dsn == nullptr || config.dsn[0] == '\0') {
        log_warning("Crash reporter: DSN not configured, crash reporting disabled");
        reporter->enabled = false;
        reporter->initialized = false;
        return reporter;
    }

    sentry_options_t* options = sentry_options_new();
    reporter->options = options;

    sentry_options_set_dsn(options, config.dsn);

    if (config.database_path != nullptr && config.database_path[0] != '\0') {
        sentry_options_set_database_path(options, config.database_path);
    } else {
        sentry_options_set_database_path(options, ".sentry-native");
    }

    if (config.release != nullptr && config.release[0] != '\0') {
        sentry_options_set_release(options, config.release);
    }

    if (config.environment != nullptr && config.environment[0] != '\0') {
        sentry_options_set_environment(options, config.environment);
    }

    sentry_options_set_max_breadcrumbs(options, 100);

    if (config.handler_path != nullptr && config.handler_path[0] != '\0') {
        sentry_options_set_handler_path(options, config.handler_path);
        log_debug("Crash reporter: using handler at %s", config.handler_path);
    }

    // FlString doesn't guarantee NUL-termination, so make an explicit NUL-terminated copy
    // before handing the path to Sentry's C API.
    FlString log_path = fl_log_file_get_path();
    if (log_path.length > 0) {
        sentry_options_add_attachment(options, string_to_cstr(arena, log_path));
        log_debug("Crash reporter: attached log file %S", log_path);
    }

    int result = sentry_init(options);
    if (result != 0) {
        log_error("Crash reporter: failed to initialize Sentry SDK (error: %d)", result);
        reporter->enabled = false;
        reporter->initialized = false;
        return reporter;
    }

    reporter->initialized = true;
    reporter->enabled = false; // Disabled by default, user must opt-in

    log_info("Crash reporter: initialized (DSN: %s, environment: %s)", config.dsn,
             config.environment != nullptr ? config.environment : "default");

    return reporter;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_reporter_destroy(CrashReporter* reporter) {
    FL_VALIDATE(reporter != nullptr);

#ifdef FL_ENABLE_SENTRY
    if (reporter->initialized) {
        // Flush any pending crash reports (blocks up to 2 seconds)
        log_debug("Crash reporter: shutting down, flushing pending reports...");
        sentry_close();
        reporter->initialized = false;
        log_debug("Crash reporter: shutdown complete");
    }
#endif

    reporter->enabled = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool crash_reporter_is_available(void) {
#ifdef FL_ENABLE_SENTRY
    return true;
#else
    return false;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool crash_reporter_is_enabled(CrashReporter* reporter) {
    FL_VALIDATE_RET(reporter != nullptr, false);
    return reporter->enabled && reporter->initialized;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_reporter_set_enabled(CrashReporter* reporter, bool enabled) {
    FL_VALIDATE(reporter != nullptr);

#ifdef FL_ENABLE_SENTRY
    if (!reporter->initialized) {
        log_warning("Crash reporter: cannot enable, not initialized");
        return;
    }

    reporter->enabled = enabled;
    log_info("Crash reporter: %s", enabled ? "enabled" : "disabled");
#else
    (void)enabled;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_reporter_set_user(CrashReporter* reporter, const char* user_id) {
    if (reporter == nullptr || user_id == nullptr) {
        return;
    }

#ifdef FL_ENABLE_SENTRY
    if (!crash_reporter_is_enabled(reporter)) {
        return;
    }

    sentry_value_t user = sentry_value_new_object();
    sentry_value_set_by_key(user, "id", sentry_value_new_string(user_id));
    sentry_set_user(user);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_reporter_set_tag(CrashReporter* reporter, const char* key, const char* value) {
    if (reporter == nullptr || key == nullptr || value == nullptr) {
        return;
    }

#ifdef FL_ENABLE_SENTRY
    if (!crash_reporter_is_enabled(reporter)) {
        return;
    }

    sentry_set_tag(key, value);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_reporter_add_breadcrumb(CrashReporter* reporter, const char* message) {
    if (reporter == nullptr || message == nullptr) {
        return;
    }

#ifdef FL_ENABLE_SENTRY
    if (!crash_reporter_is_enabled(reporter)) {
        return;
    }

    sentry_value_t crumb = sentry_value_new_breadcrumb(nullptr, message);
    sentry_add_breadcrumb(crumb);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_reporter_trigger_test_crash(void) {
#ifdef FL_ENABLE_SENTRY
    log_warning("Crash reporter: triggering test crash!");

    sentry_value_t crumb = sentry_value_new_breadcrumb("test", "Deliberate test crash triggered");
    sentry_add_breadcrumb(crumb);

    // Dereference null pointer to cause SIGSEGV
    volatile int* ptr = nullptr;
    *ptr = 42;
#else
    log_warning("Crash reporter: test crash requested but Sentry not compiled in");
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
