#include "../arena.h"
#include "../log.h"
#include "../perf_counters.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Linux performance counters backend
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Linux-specific types and constants

typedef u64 perf_counter_read_format;

#ifndef __NR_perf_event_open
#define __NR_perf_event_open 298
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int perf_event_open(struct perf_event_attr* attr, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Linux-specific session data

typedef struct PerfCounterLinuxData {
    int fds[8]; // File descriptors, slot-indexed by config index (-1 = disabled/unopened slot)
    bool is_group;
} PerfCounterLinuxData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Backend implementation

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counters_init_impl(void) {
    // Check if perf_event_open is available by trying to open a simple counter
    struct perf_event_attr attr = { 0 };
    attr.type = PERF_TYPE_HARDWARE;
    attr.size = sizeof(struct perf_event_attr);
    attr.config = PERF_COUNT_HW_CPU_CYCLES;
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;

    int fd = perf_event_open(&attr, 0, -1, -1, 0);
    if (fd < 0) {
        if (errno == EPERM || errno == EACCES) {
            log_error("Performance counters require root privileges or CAP_PERFMON capability");
        } else if (errno == ENOENT) {
            log_error("Performance counters not supported on this system");
        } else {
            log_error("Failed to initialize performance counters: %s", strerror(errno));
        }
        return false;
    }

    close(fd);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void perf_counters_shutdown_impl(void) {
    // No global state to clean up
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counters_available_impl(void) {
    struct perf_event_attr attr = { 0 };
    attr.type = PERF_TYPE_HARDWARE;
    attr.size = sizeof(struct perf_event_attr);
    attr.config = PERF_COUNT_HW_CPU_CYCLES;
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;

    int fd = perf_event_open(&attr, 0, -1, -1, 0);
    if (fd < 0) {
        return false;
    }

    close(fd);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PerfCounterSession* perf_counter_session_create_impl(struct FlArena* arena) {
    PerfCounterSession* session = arena_alloc_zero(arena, PerfCounterSession);
    PerfCounterLinuxData* linux_data = arena_alloc_zero(arena, PerfCounterLinuxData);
    session->platform_data[0] = linux_data;

    return session;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void perf_counter_session_destroy_impl(PerfCounterSession* session) {
    if (!session) {
        return;
    }

    PerfCounterLinuxData* linux_data = (PerfCounterLinuxData*)session->platform_data[0];
    if (linux_data) {
        for_count(i, session->num_counters) {
            if (linux_data->fds[i] >= 0) {
                close(linux_data->fds[i]);
                linux_data->fds[i] = -1;
            }
        }
    }

    // The session struct outlives this call (the arena owns it), so a later configure on the same
    // session must not find closed descriptor values the kernel may have reassigned elsewhere.
    session->num_counters = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counter_session_configure_impl(PerfCounterSession* session, const PerfCounterConfig* configs,
                                         u32 num_configs) {
    if (!session || !configs || num_configs == 0 || num_configs > 8) {
        return false;
    }

    PerfCounterLinuxData* linux_data = (PerfCounterLinuxData*)session->platform_data[0];
    if (!linux_data) {
        return false;
    }

    for_count(i, session->num_counters) {
        if (linux_data->fds[i] >= 0) {
            close(linux_data->fds[i]);
        }
    }

    // Mark every slot unopened up front so a partial failure below never leaves a slot holding a
    // stale (or zero-initialized) value that a later close loop would mistake for a real fd.
    for_count(i, sizeof_array(linux_data->fds)) {
        linux_data->fds[i] = -1;
    }

    memcpy(session->counters, configs, num_configs * sizeof(PerfCounterConfig));
    session->num_counters = num_configs;
    linux_data->is_group = (num_configs > 1);

    int leader_fd = -1;
    for_count(i, num_configs) {
        if (!configs[i].enabled) {
            continue;
        }

        const PerfCounterPlatformEvent platform_event = perf_counter_event_to_platform_event(configs[i].event);

        struct perf_event_attr attr = { 0 };
        attr.type = platform_event.type;
        attr.size = sizeof(struct perf_event_attr);
        attr.config = platform_event.config;
        attr.disabled = 1;
        attr.exclude_kernel = 1;
        attr.exclude_hv = 1;
        attr.read_format = 0; // Simple counter value only

        // For grouped counters, the first enabled counter is the group leader (slot 0 may be
        // disabled, so fds[0] is not necessarily a valid fd). leader_fd is -1 until the leader
        // itself is opened, which is exactly what perf_event_open expects for a leader.
        int group_fd = linux_data->is_group ? leader_fd : -1;

        int fd = perf_event_open(&attr, 0, -1, group_fd, 0);
        if (fd < 0) {
            log_error("Failed to open perf counter %u: %s", i, strerror(errno));

            for_count(j, i) {
                if (linux_data->fds[j] >= 0) {
                    close(linux_data->fds[j]);
                    linux_data->fds[j] = -1;
                }
            }
            return false;
        }

        linux_data->fds[i] = fd;
        if (leader_fd < 0) {
            leader_fd = fd;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counter_session_start_impl(PerfCounterSession* session) {
    if (!session) {
        return false;
    }

    PerfCounterLinuxData* linux_data = (PerfCounterLinuxData*)session->platform_data[0];
    if (!linux_data) {
        return false;
    }

    for_count(i, session->num_counters) {
        if (linux_data->fds[i] >= 0) {
            ioctl(linux_data->fds[i], PERF_EVENT_IOC_RESET, 0);
        }
    }

    for_count(i, session->num_counters) {
        if (linux_data->fds[i] >= 0) {
            ioctl(linux_data->fds[i], PERF_EVENT_IOC_ENABLE, 0);
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counter_session_stop_impl(PerfCounterSession* session, PerfCounterResult* result) {
    if (!session || !result) {
        return false;
    }

    PerfCounterLinuxData* linux_data = (PerfCounterLinuxData*)session->platform_data[0];
    if (!linux_data) {
        return false;
    }

    memset(result, 0, sizeof(PerfCounterResult));
    result->num_values = session->num_counters;

    for_count(i, session->num_counters) {
        if (linux_data->fds[i] >= 0) {
            ioctl(linux_data->fds[i], PERF_EVENT_IOC_DISABLE, 0);

            perf_counter_read_format read_data;
            ssize_t bytes_read = read(linux_data->fds[i], &read_data, sizeof(read_data));
            if (bytes_read == sizeof(read_data)) {
                result->values[i] = read_data;
            } else {
                log_error("Failed to read counter %u: %s", i, strerror(errno));
                result->values[i] = 0;
            }
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool perf_counter_session_reset_impl(PerfCounterSession* session) {
    if (!session) {
        return false;
    }

    PerfCounterLinuxData* linux_data = (PerfCounterLinuxData*)session->platform_data[0];
    if (!linux_data) {
        return false;
    }

    for_count(i, session->num_counters) {
        if (linux_data->fds[i] >= 0) {
            ioctl(linux_data->fds[i], PERF_EVENT_IOC_RESET, 0);
        }
    }

    return true;
}
