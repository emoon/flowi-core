#include "crash_handler.h"
#include "crash_detector.h"
#include "log.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>

#if PLATFORM_LINUX || PLATFORM_MACOS
#include <execinfo.h>
#include <sys/resource.h>
#include <errno.h>
#include <unistd.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Signal handler state

static bool g_handler_installed = false;
static CrashPhase g_current_phase = CrashPhase_Unknown;

#if PLATFORM_LINUX || PLATFORM_MACOS

// Note: These must be volatile sig_atomic_t for safe access from signal handler
static volatile sig_atomic_t g_crash_signal = 0;
static volatile sig_atomic_t g_current_phase_atomic = CrashPhase_Unknown;

// Alternate signal stack (for handling stack overflow)
#define SIGNAL_STACK_SIZE (64 * 1024)
static char g_signal_stack[SIGNAL_STACK_SIZE];

static struct sigaction g_prev_sigsegv;
static struct sigaction g_prev_sigabrt;
static struct sigaction g_prev_sigbus;
static struct sigaction g_prev_sigfpe;
static struct sigaction g_prev_sigill;

#endif // PLATFORM_LINUX || PLATFORM_MACOS

#if PLATFORM_LINUX || PLATFORM_MACOS

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Async-signal-safe write helper - log_* functions must not be used in signal handlers.

// write() may consume less than it was given and may be cut short by a signal, and it is
// declared warn_unused_result because ignoring that loses output. Everything below goes
// through this one loop. write() is async-signal-safe, so it is legal here; the loop gives
// up on any real error rather than risk spinning while the process is already going down.

static void safe_write_bytes(int fd, const char* bytes, size_t len) {
    size_t offset = 0;

    while (offset < len) {
        ssize_t written = write(fd, bytes + offset, len - offset);

        if (written <= 0) {
            if (written < 0 && errno == EINTR) {
                continue;
            }

            return;
        }

        offset += (size_t)written;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void safe_write(int fd, const char* str) {
    if (str != nullptr) {
        size_t len = 0;
        while (str[len] != '\0') {
            len++;
        }
        safe_write_bytes(fd, str, len);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void safe_write_number(int fd, int num) {
    char buf[16];
    int i = 0;

    if (num < 0) {
        safe_write_bytes(fd, "-", 1);
        num = -num;
    }

    if (num == 0) {
        safe_write_bytes(fd, "0", 1);
        return;
    }

    while (num > 0 && i < 15) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }

    while (i > 0) {
        safe_write_bytes(fd, &buf[--i], 1);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* signal_name(int signum) {
    switch (signum) {
        case SIGSEGV:
            return "SIGSEGV (Segmentation fault)";
        case SIGABRT:
            return "SIGABRT (Aborted)";
        case SIGBUS:
            return "SIGBUS (Bus error)";
        case SIGFPE:
            return "SIGFPE (Floating point exception)";
        case SIGILL:
            return "SIGILL (Illegal instruction)";
        default:
            return "Unknown signal";
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* phase_name(CrashPhase phase) {
    switch (phase) {
        case CrashPhase_Init:
            return "initialization";
        case CrashPhase_PluginLoad:
            return "plugin loading";
        case CrashPhase_Rendering:
            return "rendering";
        case CrashPhase_Running:
            return "running";
        default:
            return "unknown";
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Signal handler - MUST be async-signal-safe!

static void crash_signal_handler(int signum) {
    // Prevent recursive crashes
    if (g_crash_signal != 0) {
        _exit(128 + signum);
    }
    g_crash_signal = signum;

    safe_write(STDERR_FILENO, "\n=== CRASH DETECTED ===\n");
    safe_write(STDERR_FILENO, "Signal: ");
    safe_write(STDERR_FILENO, signal_name(signum));
    safe_write(STDERR_FILENO, " (");
    safe_write_number(STDERR_FILENO, signum);
    safe_write(STDERR_FILENO, ")\n");
    safe_write(STDERR_FILENO, "Phase: ");
    safe_write(STDERR_FILENO, phase_name((CrashPhase)g_current_phase_atomic));
    safe_write(STDERR_FILENO, "\n");

    safe_write(STDERR_FILENO, "\nBacktrace:\n");

    void* bt_buffer[64];
    int bt_size = backtrace(bt_buffer, 64);

    // backtrace_symbols_fd is async-signal-safe on most systems
    backtrace_symbols_fd(bt_buffer, bt_size, STDERR_FILENO);

    safe_write(STDERR_FILENO, "\n=== END CRASH INFO ===\n\n");

    crash_detector_record_crash_from_signal((u32)signum, (CrashPhase)g_current_phase_atomic);

    // Re-raise the signal with default handler to get proper exit code
    signal(signum, SIG_DFL);
    raise(signum);
}

#endif // PLATFORM_LINUX || PLATFORM_MACOS

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_handler_install(void) {
    if (g_handler_installed) {
        return;
    }

#if PLATFORM_LINUX || PLATFORM_MACOS
    // Resolve the crash-state path now so the signal handler never has to allocate
    crash_detector_prepare_for_signal();

    // glibc's backtrace() lazily loads libgcc (which mallocs) on first use; warm it up
    // here so the call inside the signal handler is malloc-free
    void* warmup[4];
    (void)backtrace(warmup, 4);

    stack_t ss;
    ss.ss_sp = g_signal_stack;
    ss.ss_size = SIGNAL_STACK_SIZE;
    ss.ss_flags = 0;

    if (sigaltstack(&ss, nullptr) != 0) {
        log_warning("Failed to set alternate signal stack");
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = crash_signal_handler;
    sa.sa_flags = SA_ONSTACK;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, &g_prev_sigsegv);
    sigaction(SIGABRT, &sa, &g_prev_sigabrt);
    sigaction(SIGBUS, &sa, &g_prev_sigbus);
    sigaction(SIGFPE, &sa, &g_prev_sigfpe);
    sigaction(SIGILL, &sa, &g_prev_sigill);

    g_handler_installed = true;
    log_debug("Crash handler installed");
#else
    log_warning("Crash handler not implemented for this platform");
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_handler_uninstall(void) {
    if (!g_handler_installed) {
        return;
    }

#if PLATFORM_LINUX || PLATFORM_MACOS
    sigaction(SIGSEGV, &g_prev_sigsegv, nullptr);
    sigaction(SIGABRT, &g_prev_sigabrt, nullptr);
    sigaction(SIGBUS, &g_prev_sigbus, nullptr);
    sigaction(SIGFPE, &g_prev_sigfpe, nullptr);
    sigaction(SIGILL, &g_prev_sigill, nullptr);

    g_handler_installed = false;
    log_debug("Crash handler uninstalled");
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_handler_set_phase(CrashPhase phase) {
    g_current_phase = phase;
#if PLATFORM_LINUX || PLATFORM_MACOS
    g_current_phase_atomic = (sig_atomic_t)phase;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CrashPhase crash_handler_get_phase(void) {
    return g_current_phase;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void crash_handler_enable_core_dumps(void) {
#ifndef NDEBUG
#if PLATFORM_LINUX || PLATFORM_MACOS
    struct rlimit core_limit;
    core_limit.rlim_cur = RLIM_INFINITY;
    core_limit.rlim_max = RLIM_INFINITY;

    if (setrlimit(RLIMIT_CORE, &core_limit) == 0) {
        log_info("Core dumps enabled (debug build)");
    } else {
        log_warning("Failed to enable core dumps");
    }
#endif
#else
    (void)0;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
