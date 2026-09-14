#include "crash_detector.h"
#include "app_identity.h"
#include "arena.h"
#include "string.h"
#include "log.h"
#include "os/os.h"
#include "path.h"
#include <string.h>
#include <time.h>

#if PLATFORM_LINUX || PLATFORM_MACOS
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// On-disk crash state file: this struct written out verbatim.

#define CRASH_STATE_MAGIC 0x52504352 // "RPCR"
#define CRASH_STATE_VERSION 1

typedef struct CrashState {
    u32 magic;
    u32 version;
    u32 record_count;
    u32 safe_mode_active;
    u64 last_successful_startup;
    CrashRecord records[CRASH_HISTORY_SIZE];
} CrashState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString s_data_dir_override = { 0 };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u64 get_current_timestamp(void) {
    return (u64)time(nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_detector_set_data_dir(FlString path) {
    if (s_data_dir_override.length > 0) {
        string_free(s_data_dir_override);
    }
    s_data_dir_override = string_copy_malloc(path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString get_app_data_dir(FlArena* arena) {
    if (s_data_dir_override.length > 0) {
        return string_copy(arena, s_data_dir_override);
    }

    const char* home = getenv_os("HOME");
    if (home == nullptr) {
        return (FlString) { 0 };
    }

    FlString home_str = string_from_cstr(home);
    return file_build_path_os(arena, home_str, fl_app_identity_data_dir_name());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString get_crash_state_path(FlArena* arena) {
    FlString dir_path = get_app_data_dir(arena);
    if (dir_path.length == 0) {
        return (FlString) { 0 };
    }
    return file_build_path_os(arena, dir_path, S("crash-state.bin"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ensure_crash_state_dir(FlArena* arena) {
    FlString dir_path = get_app_data_dir(arena);
    if (dir_path.length == 0) {
        return false;
    }

    if (file_directory_exists_os(dir_path)) {
        return true;
    }

    return file_create_directory_recursive_os(dir_path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static CrashState load_crash_state(void) {
    CrashState state = { 0 };
    state.magic = CRASH_STATE_MAGIC;
    state.version = CRASH_STATE_VERSION;

    arena_scratch_auto(temp);
    FlString path = get_crash_state_path(temp.arena);
    if (path.length == 0) {
        return state;
    }

    if (!file_exists_os(path)) {
        return state;
    }

    CrashState loaded = { 0 };
    i64 bytes_read = file_read_os((u8*)&loaded, path, sizeof(CrashState));

    if (bytes_read != sizeof(CrashState)) {
        log_warning("Crash state file corrupted, resetting");
        return state;
    }

    if (loaded.magic != CRASH_STATE_MAGIC) {
        log_warning("Crash state file has invalid magic, resetting");
        return state;
    }

    if (loaded.version != CRASH_STATE_VERSION) {
        log_warning("Crash state file version mismatch (got %u, expected %u), resetting", loaded.version,
                    CRASH_STATE_VERSION);
        return state;
    }

    return loaded;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool save_crash_state(const CrashState* state) {
    arena_scratch_auto(temp);

    if (!ensure_crash_state_dir(temp.arena)) {
        log_error("Failed to create crash state directory");
        return false;
    }

    FlString path = get_crash_state_path(temp.arena);
    if (path.length == 0) {
        return false;
    }

    i64 bytes_written = file_write_os(path, (const u8*)state, sizeof(CrashState));

    if (bytes_written != sizeof(CrashState)) {
        log_error("Failed to write crash state file: %S", path);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CrashLoopStatus crash_detector_check(FlArena* arena) {
    (void)arena;

    CrashState state = load_crash_state();

    if (state.safe_mode_active) {
        log_info("Safe mode is active from previous session");
        return CrashLoopStatus_SafeModeActive;
    }

    u64 now = get_current_timestamp();

    u32 recent_crashes = 0;
    u32 count = state.record_count < CRASH_HISTORY_SIZE ? state.record_count : CRASH_HISTORY_SIZE;

    for (u32 i = 0; i < count; i++) {
        u64 crash_time = state.records[i].timestamp;
        if (now >= crash_time && (now - crash_time) <= CRASH_LOOP_WINDOW_SECONDS) {
            recent_crashes++;
        }
    }

    if (recent_crashes >= CRASH_LOOP_THRESHOLD) {
        log_error("Crash loop detected: %u crashes in last %d seconds", recent_crashes, CRASH_LOOP_WINDOW_SECONDS);

        state.safe_mode_active = 1;
        save_crash_state(&state);

        return CrashLoopStatus_Detected;
    }

    if (recent_crashes > 0) {
        log_warning("Previous crash detected (%u recent crashes)", recent_crashes);
    }

    return CrashLoopStatus_None;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_detector_record_crash(u32 exit_code, CrashPhase phase) {
    CrashState state = load_crash_state();

    u32 index = state.record_count % CRASH_HISTORY_SIZE;
    state.records[index].timestamp = get_current_timestamp();
    state.records[index].exit_code = exit_code;
    state.records[index].startup_phase = (u32)phase;
    state.record_count++;

    save_crash_state(&state);

    log_debug("Recorded crash: exit_code=%u, phase=%u", exit_code, phase);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Signal-safe crash recording
//
// A SIGSEGV/SIGABRT handler may run while the crashing thread holds the malloc lock, the log mutex
// or an arena, so nothing below allocates, logs or uses stdio: the path is resolved once in normal
// context and the record uses only async-signal-safe syscalls (open/read/write/close/lseek/time).

#if PLATFORM_LINUX || PLATFORM_MACOS

static char s_signal_safe_path[4096];
static volatile sig_atomic_t s_signal_safe_ready = 0;

void crash_detector_prepare_for_signal(void) {
    s_signal_safe_ready = 0;

    arena_scratch_auto(temp);

    if (!ensure_crash_state_dir(temp.arena)) {
        log_warning("Failed to create crash state directory; signal-safe crash recording disabled");
        return;
    }

    FlString path = get_crash_state_path(temp.arena);
    if (path.length == 0 || path.length >= sizeof(s_signal_safe_path)) {
        log_warning("Crash state path unavailable; signal-safe crash recording disabled");
        return;
    }

    string_to_cstr_buffer(s_signal_safe_path, sizeof(s_signal_safe_path), path);
    s_signal_safe_ready = 1;
}

void crash_detector_record_crash_from_signal(u32 exit_code, CrashPhase phase) {
    if (!s_signal_safe_ready) {
        return;
    }

    CrashState state = { 0 };
    state.magic = CRASH_STATE_MAGIC;
    state.version = CRASH_STATE_VERSION;

    int fd = open(s_signal_safe_path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        return;
    }

    CrashState loaded;
    i64 bytes_read = (i64)read(fd, &loaded, sizeof(loaded));
    if (bytes_read == (i64)sizeof(loaded) && loaded.magic == CRASH_STATE_MAGIC
        && loaded.version == CRASH_STATE_VERSION) {
        state = loaded;
    }

    u32 index = state.record_count % CRASH_HISTORY_SIZE;
    state.records[index].timestamp = (u64)time(nullptr);
    state.records[index].exit_code = exit_code;
    state.records[index].startup_phase = (u32)phase;
    state.record_count++;

    if (lseek(fd, 0, SEEK_SET) == 0) {
        i64 written = (i64)write(fd, &state, sizeof(state));
        (void)written;
    }
    close(fd);
}

#else

void crash_detector_prepare_for_signal(void) {}

void crash_detector_record_crash_from_signal(u32 exit_code, CrashPhase phase) {
    // No POSIX signal-context restrictions on this platform; use the normal path.
    crash_detector_record_crash(exit_code, phase);
}

#endif // PLATFORM_LINUX || PLATFORM_MACOS

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_detector_record_successful_startup(void) {
    CrashState state = load_crash_state();

    state.last_successful_startup = get_current_timestamp();
    save_crash_state(&state);

    log_debug("Recorded successful startup");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_detector_clear_on_graceful_shutdown(void) {
    CrashState state = load_crash_state();

    if (state.last_successful_startup == 0) {
        return;
    }

    u64 now = get_current_timestamp();

    // The wall clock can step backward between startup and shutdown (NTP correction, manual clock
    // change, VM resume), and the unsigned subtraction below would underflow to a huge elapsed time.
    if (now < state.last_successful_startup) {
        log_debug("Clock stepped backward since startup, keeping crash history");
        return;
    }

    u64 uptime = now - state.last_successful_startup;

    if (uptime >= 60) {
        // A session that ran 60+ seconds counts as successful
        state.record_count = 0;
        state.safe_mode_active = 0;
        save_crash_state(&state);

        log_debug("Graceful shutdown after %llu seconds, cleared crash history", (unsigned long long)uptime);
    } else {
        log_debug("Short session (%llu seconds), keeping crash history", (unsigned long long)uptime);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool crash_detector_is_safe_mode_active(void) {
    CrashState state = load_crash_state();
    return state.safe_mode_active != 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_detector_exit_safe_mode(void) {
    CrashState state = load_crash_state();

    if (state.safe_mode_active) {
        state.safe_mode_active = 0;
        state.record_count = 0;
        save_crash_state(&state);

        log_info("Exited safe mode, crash history cleared");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
