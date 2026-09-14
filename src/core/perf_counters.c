#include "perf_counters.h"
#include "log.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Performance counters - platform-independent core
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool g_perf_counters_initialized = false;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counters_init(void) {
    if (g_perf_counters_initialized) {
        return true;
    }

    if (!perf_counters_init_impl()) {
        log_error("Failed to initialize platform performance counters");
        return false;
    }

    g_perf_counters_initialized = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void perf_counters_shutdown(void) {
    if (!g_perf_counters_initialized) {
        return;
    }

    perf_counters_shutdown_impl();
    g_perf_counters_initialized = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counters_available(void) {
    if (!g_perf_counters_initialized) {
        return false;
    }

    return perf_counters_available_impl();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PerfCounterSession* perf_counter_session_create(struct FlArena* arena) {
    if (!g_perf_counters_initialized) {
        log_error("Performance counters not initialized");
        return nullptr;
    }

    return perf_counter_session_create_impl(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void perf_counter_session_destroy(PerfCounterSession* session) {
    if (!session) {
        return;
    }

    perf_counter_session_destroy_impl(session);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counter_session_configure(PerfCounterSession* session, const PerfCounterConfig* configs, u32 num_configs) {
    if (!session || !configs || num_configs == 0 || num_configs > 8) {
        log_error("Invalid parameters for perf_counter_session_configure");
        return false;
    }

    return perf_counter_session_configure_impl(session, configs, num_configs);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counter_session_start(PerfCounterSession* session) {
    if (!session) {
        log_error("Invalid session for perf_counter_session_start");
        return false;
    }

    if (session->is_active) {
        log_error("Session is already active");
        return false;
    }

    if (!perf_counter_session_start_impl(session)) {
        return false;
    }

    session->is_active = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counter_session_stop(PerfCounterSession* session, PerfCounterResult* result) {
    if (!session || !result) {
        log_error("Invalid parameters for perf_counter_session_stop");
        return false;
    }

    if (!session->is_active) {
        log_error("Session is not active");
        return false;
    }

    if (!perf_counter_session_stop_impl(session, result)) {
        return false;
    }

    session->is_active = false;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counter_session_reset(PerfCounterSession* session) {
    if (!session) {
        log_error("Invalid session for perf_counter_session_reset");
        return false;
    }

    if (!session->is_active) {
        log_error("Session is not active");
        return false;
    }

    return perf_counter_session_reset_impl(session);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString perf_counter_session_get_name(const PerfCounterSession* session, u32 index) {
    if (!session || index >= session->num_counters) {
        return S("");
    }
    return session->counters[index].name;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if PLATFORM_LINUX
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// PERF_TYPE_HW_CACHE packs its event as (cache id) | (operation << 8) | (result << 16).
#define HW_CACHE_CONFIG(cache, op, result)                                      \
    ((u64)PERF_COUNT_HW_CACHE_##cache | ((u64)PERF_COUNT_HW_CACHE_OP_##op << 8) \
     | ((u64)PERF_COUNT_HW_CACHE_RESULT_##result << 16))

#define HW_EVENT(config_value) ((PerfCounterPlatformEvent) { PERF_TYPE_HARDWARE, (u64)(config_value) })
#define HW_CACHE_EVENT(config_value) ((PerfCounterPlatformEvent) { PERF_TYPE_HW_CACHE, (config_value) })

PerfCounterPlatformEvent perf_counter_event_to_platform_event(PerfCounterEvent event) {
    switch (event) {
        case PERF_COUNTER_CYCLES:
            return HW_EVENT(PERF_COUNT_HW_CPU_CYCLES);
        case PERF_COUNTER_INSTRUCTIONS:
            return HW_EVENT(PERF_COUNT_HW_INSTRUCTIONS);
        case PERF_COUNTER_CACHE_L1D_MISS:
            return HW_CACHE_EVENT(HW_CACHE_CONFIG(L1D, READ, MISS));
        case PERF_COUNTER_CACHE_L1D_ACCESS:
            return HW_CACHE_EVENT(HW_CACHE_CONFIG(L1D, READ, ACCESS));
        case PERF_COUNTER_CACHE_L1I_MISS:
            return HW_CACHE_EVENT(HW_CACHE_CONFIG(L1I, READ, MISS));
        case PERF_COUNTER_CACHE_L1I_ACCESS:
            return HW_CACHE_EVENT(HW_CACHE_CONFIG(L1I, READ, ACCESS));
        // The ABI exposes the last cache level rather than L2 by name; on a three-level CPU these
        // two count L3.
        case PERF_COUNTER_CACHE_L2_MISS:
            return HW_CACHE_EVENT(HW_CACHE_CONFIG(LL, READ, MISS));
        case PERF_COUNTER_CACHE_L2_ACCESS:
            return HW_CACHE_EVENT(HW_CACHE_CONFIG(LL, READ, ACCESS));
        case PERF_COUNTER_BRANCH_MISSES:
            return HW_EVENT(PERF_COUNT_HW_BRANCH_MISSES);
        case PERF_COUNTER_BRANCH_INSTRUCTIONS:
            return HW_EVENT(PERF_COUNT_HW_BRANCH_INSTRUCTIONS);
        // The ABI has no load/store event: a load is an L1D read access and a store an L1D write
        // access, so MEMORY_LOADS and CACHE_L1D_ACCESS select the same counter.
        case PERF_COUNTER_MEMORY_STORES:
            return HW_CACHE_EVENT(HW_CACHE_CONFIG(L1D, WRITE, ACCESS));
        case PERF_COUNTER_MEMORY_LOADS:
            return HW_CACHE_EVENT(HW_CACHE_CONFIG(L1D, READ, ACCESS));
        case PERF_COUNTER_TLB_MISS:
            return HW_CACHE_EVENT(HW_CACHE_CONFIG(DTLB, READ, MISS));
        case PERF_COUNTER_STALL_FRONTEND:
            // Frontend stall cycles - not directly available, use cycles as fallback
            return HW_EVENT(PERF_COUNT_HW_CPU_CYCLES);
        case PERF_COUNTER_STALL_BACKEND:
            // Backend stall cycles - not directly available, use cycles as fallback
            return HW_EVENT(PERF_COUNT_HW_CPU_CYCLES);
        default:
            return HW_EVENT(0);
    }
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString perf_counter_event_to_string(PerfCounterEvent event) {
    switch (event) {
        case PERF_COUNTER_CYCLES:
            return S("CPU Cycles");
        case PERF_COUNTER_INSTRUCTIONS:
            return S("Instructions");
        case PERF_COUNTER_CACHE_L1D_MISS:
            return S("L1 Data Cache Misses");
        case PERF_COUNTER_CACHE_L1D_ACCESS:
            return S("L1 Data Cache Accesses");
        case PERF_COUNTER_CACHE_L1I_MISS:
            return S("L1 Instruction Cache Misses");
        case PERF_COUNTER_CACHE_L1I_ACCESS:
            return S("L1 Instruction Cache Accesses");
        case PERF_COUNTER_CACHE_L2_MISS:
            return S("L2 Cache Misses");
        case PERF_COUNTER_CACHE_L2_ACCESS:
            return S("L2 Cache Accesses");
        case PERF_COUNTER_BRANCH_MISSES:
            return S("Branch Misses");
        case PERF_COUNTER_BRANCH_INSTRUCTIONS:
            return S("Branch Instructions");
        case PERF_COUNTER_MEMORY_STORES:
            return S("Memory Stores");
        case PERF_COUNTER_MEMORY_LOADS:
            return S("Memory Loads");
        case PERF_COUNTER_TLB_MISS:
            return S("TLB Misses");
        case PERF_COUNTER_STALL_FRONTEND:
            return S("Frontend Stalls");
        case PERF_COUNTER_STALL_BACKEND:
            return S("Backend Stalls");
        default:
            return S("Unknown Event");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if PLATFORM_LINUX
#include "os/perf_counters_linux.c"
#elif PLATFORM_WINDOWS
#include "os/perf_counters_windows.c"
#elif PLATFORM_MACOS
#include "os/perf_counters_macos.c"
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
