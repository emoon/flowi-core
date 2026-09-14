#include "../file_watcher_internal.h"
#include "../fixed_array.h"
#include "../log.h"
#include "../path.h"
#include "os.h"

#if PLATFORM_LINUX

#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static LogChannelId FW_ID = LOG_CHANNEL_INVALID;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct WatchEntry {
    int wd;
    FlString path;
} WatchEntry;

typedef struct LinuxWatcherData {
    int inotify_fd;
    u32 notify_mask;
    bool recursive;
    fixed_array(WatchEntry) watches;
} LinuxWatcherData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileWatcherError map_errno_to_error(int err) {
    switch (err) {
        case EACCES:
            return FileWatcherError_PermissionDenied;
        case ENOENT:
        case ENOTDIR:
            return FileWatcherError_InvalidPath;
        case EMFILE:
        case ENFILE:
        case ENOSPC:
            return FileWatcherError_ResourceExhausted;
        default:
            return FileWatcherError_SystemError;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString find_path_for_wd(LinuxWatcherData* data, int wd) {
    for (u32 i = 0; i < fixed_array_length(&data->watches); i++) {
        WatchEntry* entry = fixed_array_get_ptr(&data->watches, i);
        if (entry && entry->wd == wd) {
            return entry->path;
        }
    }
    return string_empty();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Returns 0 on success, otherwise the errno of the failing inotify_add_watch. Captured here because
// the logging below may set errno itself, so the caller cannot read it after the fact.
static int add_watch_for_directory(LinuxWatcherData* data, FlString path, FlArena* arena) {
    OsPath os_path = string_to_os_path(arena, path);
    int wd = inotify_add_watch(data->inotify_fd, os_path_str(os_path), data->notify_mask);
    if (wd == -1) {
        const int add_watch_errno = errno;
        logc_warning(FW_ID, "Failed to add watch for %S: %d", path, add_watch_errno);
        return add_watch_errno;
    }

    WatchEntry entry = { .wd = wd, .path = string_copy(get_file_watcher_registry()->arena, path) };
    fixed_array_add(&data->watches, entry);

    logc_debug(FW_ID, "Added watch (wd: %d) for: %S", wd, path);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void add_watches_recursive(LinuxWatcherData* data, FlString base_path, FlArena* temp_arena) {
    DirHandle dir = dir_open_os(temp_arena, base_path);
    if (!dir.is_valid) {
        return;
    }

    FileStat entry;
    while ((entry = dir_read_next_os(temp_arena, &dir)).is_valid) {
        if (!entry.is_directory) {
            continue;
        }
        if (entry.name.length > 0 && entry.name.data[0] == '.') {
            continue;
        }

        FlString subdir_path = path_join(temp_arena, base_path, entry.name);

        add_watch_for_directory(data, subdir_path, temp_arena);
        add_watches_recursive(data, subdir_path, temp_arena);
    }

    dir_close_os(&dir);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileWatcherError platform_start_watching(FileWatcherState* state) {
    if (FW_ID == LOG_CHANNEL_INVALID) {
        FW_ID = fl_log_register_channel(S("FILE_WATCHER"));
    }

    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd == -1) {
        return map_errno_to_error(errno);
    }

    u32 mask = 0;
    if (state->config.watch_files) {
        mask |= IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO;
    }
    if (state->config.watch_directories || state->config.recursive) {
        // Need directory events to track new/deleted subdirectories for recursive watching
        mask |= IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
    }

    LinuxWatcherData* data = arena_alloc(get_file_watcher_registry()->arena, LinuxWatcherData);
    data->inotify_fd = fd;
    data->notify_mask = mask;
    data->recursive = state->config.recursive;
    fixed_array_new(&data->watches, get_file_watcher_registry()->arena, 32);

    const int watch_errno = add_watch_for_directory(data, state->watch_path, get_file_watcher_registry()->arena);
    if (watch_errno != 0) {
        close(fd);
        return map_errno_to_error(watch_errno);
    }

    if (state->config.recursive) {
        arena_scratch_auto(temp);
        add_watches_recursive(data, state->watch_path, temp.arena);
        logc_info(FW_ID, "FileWatcher[%u]: Added %u watches for recursive monitoring", state->handle,
                  fixed_array_length(&data->watches));
    }

    state->platform_data = data;

    logc_debug(FW_ID, "FileWatcher[%u]: Linux inotify watcher initialized (fd: %d, watches: %u)", state->handle, fd,
               fixed_array_length(&data->watches));

    return FileWatcherError_Success;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void platform_stop_watching(FileWatcherState* state) {
    if (!state || !state->platform_data) {
        return;
    }

    LinuxWatcherData* data = (LinuxWatcherData*)state->platform_data;

    for (u32 i = 0; i < fixed_array_length(&data->watches); i++) {
        WatchEntry* entry = fixed_array_get_ptr(&data->watches, i);
        if (entry) {
            inotify_rm_watch(data->inotify_fd, entry->wd);
        }
    }

    if (data->inotify_fd != -1) {
        close(data->inotify_fd);
    }

    state->platform_data = nullptr;

    logc_debug(FW_ID, "FileWatcher[%u]: Linux inotify watcher stopped", state->handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u32 map_inotify_mask_to_change_types(u32 mask) {
    u32 change_types = 0;

    if (mask & IN_CREATE) {
        change_types |= FlFileChangeType_Created;
    }
    if (mask & IN_MODIFY) {
        change_types |= FlFileChangeType_Modified;
    }
    if (mask & IN_DELETE) {
        change_types |= FlFileChangeType_Deleted;
    }
    if (mask & (IN_MOVED_FROM | IN_MOVED_TO)) {
        change_types |= FlFileChangeType_Moved;
    }

    return change_types;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileWatcherError platform_poll_changes(FileWatcherState* state) {
    if (!state || !state->platform_data || !state->is_active) {
        return FileWatcherError_Success;
    }

    LinuxWatcherData* data = (LinuxWatcherData*)state->platform_data;

    char buffer[4096];

    while (true) {
        ssize_t length = read(data->inotify_fd, buffer, sizeof(buffer));

        if (length == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                logc_error(FW_ID, "FileWatcher[%u]: inotify read error: %d", state->handle, errno);
                state->is_active = false;
                return FileWatcherError_SystemError;
            }
        }

        if (length == 0) {
            break;
        }

        char* ptr = buffer;
        while (ptr < buffer + length) {
            struct inotify_event* event = (struct inotify_event*)ptr;

            // The kernel dropped this watch (directory deleted/moved/unmounted). Prune the
            // wd -> path entry so a later kernel-reused wd can't resolve to the stale path.
            // IN_IGNORED carries no name, so it must be handled before the len check below.
            if (event->mask & IN_IGNORED) {
                for (u32 i = 0; i < fixed_array_length(&data->watches); i++) {
                    WatchEntry* entry = fixed_array_get_ptr(&data->watches, i);
                    if (entry && entry->wd == event->wd) {
                        fixed_array_remove_swap(&data->watches, i);
                        break;
                    }
                }
                ptr += sizeof(struct inotify_event) + event->len;
                continue;
            }

            // Skip events without names
            if (event->len > 0) {
                FlString watch_path = find_path_for_wd(data, event->wd);
                if (string_is_empty(watch_path)) {
                    ptr += sizeof(struct inotify_event) + event->len;
                    continue;
                }

                FlArena* temp_arena = arena_new();
                FlString filename = string_from_cstr(event->name);
                FlString full_path = path_join(temp_arena, watch_path, filename);

                u32 change_types = map_inotify_mask_to_change_types(event->mask);
                bool is_directory = (event->mask & IN_ISDIR) != 0;

                if (data->recursive && is_directory && (event->mask & (IN_CREATE | IN_MOVED_TO))) {
                    add_watch_for_directory(data, full_path, temp_arena);
                    if (event->mask & IN_MOVED_TO) {
                        // Unlike a fresh mkdir, a moved-in directory can already have subdirectories
                        add_watches_recursive(data, full_path, temp_arena);
                    }
                }

                if (change_types != 0) {
                    logc_debug(FW_ID, "FileWatcher[%u]: %S (types: 0x%x, dir: %d)", state->handle, full_path,
                               change_types, is_directory);
                    add_file_change(state, full_path, S(""), change_types, is_directory);
                }

                arena_destroy(temp_arena);
            }

            ptr += sizeof(struct inotify_event) + event->len;
        }
    }

    return FileWatcherError_Success;
}

#endif // PLATFORM_LINUX