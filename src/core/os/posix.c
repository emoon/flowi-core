#define _GNU_SOURCE // Must be first for pthread_setaffinity_np
#include "../core.h"
#include "../memory.h"
#include "os.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <unistd.h>
#if PLATFORM_LINUX
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#endif
#if PLATFORM_MACOS
#include <mach-o/dyld.h>
#endif
#include "../arena.h"
#include "../sprintf.h"
#include "../math.h"
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OS Path conversion - POSIX just uses UTF-8 strings directly

OsPath string_to_os_path(struct FlArena* arena, FlString path) {
    OsPath result;
    result.cstr_path = (char*)string_to_cstr(arena, path);
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_get_size_os(FlString path) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);
    struct stat st;
    i64 size = -1;
    if (stat(os_path_str(os_path), &st) == 0) {
        size = st.st_size;
    }
    return size;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_read_from_handle_os(u8* dest, const i64 fd, const i64 file_size, const i64 buffer_size) {
    const i64 read_size = min(buffer_size, file_size);
    // A negative size (e.g. file_get_size_os's -1 failure sentinel) would be reinterpreted
    // as a huge size_t by read(), turning it into an unbounded over-read of dest.
    if (read_size < 0) {
        return -1;
    }
    return read(fd, dest, (size_t)read_size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_read_os(u8* dest, FlString path, const i64 buffer_size) {
    const i64 fd = file_open_os(path, 0);
    if (fd < 0) {
        return -1;
    }

    const i64 file_size = file_get_size_os(path);
    if (file_size < 0) {
        file_close_handle_os(fd);
        return -1;
    }

    const i64 ret_size = file_read_from_handle_os(dest, fd, file_size, buffer_size);
    file_close_handle_os(fd);

    return ret_size;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_open_os(FlString path, const int flags) {
    (void)flags;
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);
    const int fd = open(os_path_str(os_path), O_RDONLY);
    return fd >= 0 ? fd : -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void file_close_handle_os(const i64 fd) {
    close((int)fd);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileMmap file_mmap_open_os(FlString path) {
    FileMmap result = { .data = nullptr, .size = 0, .fd = -1 };

    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);

    int fd = open(os_path_str(os_path), O_RDONLY);
    if (fd < 0) {
        return result;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return result;
    }

    void* mapped = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return result;
    }

    result.data = (const u8*)mapped;
    result.size = (u64)st.st_size;
    result.fd = fd;
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void file_mmap_close_os(FileMmap* mmap) {
    if (mmap == nullptr || mmap->data == nullptr) {
        return;
    }

    munmap((void*)mmap->data, (size_t)mmap->size);
    close(mmap->fd);
    mmap->data = nullptr;
    mmap->size = 0;
    mmap->fd = -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_open_write_os(FlString path, bool append) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);

    int flags = O_WRONLY | O_CREAT;
    if (append) {
        flags |= O_APPEND;
    } else {
        flags |= O_TRUNC;
    }

    const int fd = open(os_path_str(os_path), flags, 0644);
    return fd >= 0 ? fd : -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_write_to_handle_os(i64 fd, const u8* data, i64 size) {
    if (fd < 0 || data == nullptr || size <= 0) {
        return -1;
    }

    ssize_t written = write((int)fd, data, (size_t)size);
    return written >= 0 ? (i64)written : -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_rename_os(FlString old_path, FlString new_path) {
    arena_scratch_auto(temp);
    OsPath old_os_path = string_to_os_path(temp.arena, old_path);
    OsPath new_os_path = string_to_os_path(temp.arena, new_path);

    return rename(os_path_str(old_os_path), os_path_str(new_os_path)) == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// write() is allowed to consume less than it was given - a pipe with a full buffer is the
// usual way - and both callers below return void, so there is nobody left to retry: either
// the loop lives here or the tail of the string is silently dropped. EINTR is not a
// failure either, only a signal that landed mid-call.

static void write_all(int fd, const char* data, size_t len) {
    size_t offset = 0;

    while (offset < len) {
        ssize_t written = write(fd, data + offset, len - offset);

        if (written <= 0) {
            if (written < 0 && errno == EINTR) {
                continue;
            }

            // Nothing useful to do with a broken stdout, and no way to report it.
            return;
        }

        offset += (size_t)written;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void stdout_write_os(FlString str) {
    write_all(STDOUT_FILENO, str.data, str.length);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void stderr_write_os(FlString str) {
    write_all(STDERR_FILENO, str.data, str.length);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 get_current_time_us_os(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (i64)ts.tv_sec * 1000000LL + (i64)ts.tv_nsec / 1000LL;
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 get_monotonic_time_ns_os(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (u64)ts.tv_sec * 1000000000ULL + (u64)ts.tv_nsec;
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString timestamp_format_os(struct FlArena* arena, i64 timestamp_us) {
    time_t seconds = (time_t)(timestamp_us / 1000000LL);
    i64 microseconds = timestamp_us % 1000000LL;

    struct tm local_time;
    if (localtime_r(&seconds, &local_time) == nullptr) {
        return string_empty();
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    return sprintf_arena(arena, "%04d:%02d:%02d %02d:%02d:%02d.%06d", local_time.tm_year + 1900, local_time.tm_mon + 1,
                         local_time.tm_mday, local_time.tm_hour, local_time.tm_min, local_time.tm_sec,
                         (int)microseconds);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(sizeof(pthread_mutex_t) <= sizeof(((Mutex*)0)->data), "pthread_mutex_t too large for Mutex struct");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void mutex_init(Mutex* mutex) {
    pthread_mutex_t* handle = (pthread_mutex_t*)mutex->data;
    pthread_mutex_init(handle, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void mutex_lock(Mutex* mutex) {
    pthread_mutex_t* handle = (pthread_mutex_t*)mutex->data;
    pthread_mutex_lock(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void mutex_unlock(Mutex* mutex) {
    pthread_mutex_t* handle = (pthread_mutex_t*)mutex->data;
    pthread_mutex_unlock(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void mutex_destroy(Mutex* mutex) {
    pthread_mutex_t* handle = (pthread_mutex_t*)mutex->data;
    pthread_mutex_destroy(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(sizeof(pthread_cond_t) <= sizeof(((CondVar*)0)->data), "pthread_cond_t too large for CondVar struct");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void condvar_init(CondVar* condvar) {
    pthread_cond_t* handle = (pthread_cond_t*)condvar->data;
    pthread_cond_init(handle, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void condvar_wait(CondVar* condvar, Mutex* mutex) {
    pthread_cond_t* cond_handle = (pthread_cond_t*)condvar->data;
    pthread_mutex_t* mutex_handle = (pthread_mutex_t*)mutex->data;
    pthread_cond_wait(cond_handle, mutex_handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void condvar_signal(CondVar* condvar) {
    pthread_cond_t* handle = (pthread_cond_t*)condvar->data;
    pthread_cond_signal(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void condvar_broadcast(CondVar* condvar) {
    pthread_cond_t* handle = (pthread_cond_t*)condvar->data;
    pthread_cond_broadcast(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void condvar_destroy(CondVar* condvar) {
    pthread_cond_t* handle = (pthread_cond_t*)condvar->data;
    pthread_cond_destroy(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory management functions

u64 get_page_size_os(void) {
    return (u64)sysconf(_SC_PAGESIZE);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ArenaError reserve_range_os(u64 size, void** out_ptr) {
    void* ptr = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        return ARENA_RESERVE_FAILED;
    }
    *out_ptr = ptr;
    return ARENA_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ArenaError commit_memory_os(void* ptr, u64 size) {
    if (mprotect(ptr, size, PROT_READ | PROT_WRITE) != 0) {
        return ARENA_PROTECTION_FAILED;
    }
    return ARENA_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ArenaError decommit_memory_os(void* ptr, u64 size) {
    if (mprotect(ptr, size, PROT_NONE) != 0) {
        return ARENA_PROTECTION_FAILED;
    }
    return ARENA_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ArenaError release_range_os(void* ptr, u64 size) {
    if (munmap(ptr, size) != 0) {
        return ARENA_PROTECTION_FAILED;
    }
    return ARENA_SUCCESS;
}

#ifndef NDEBUG

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ArenaError protect_memory_os(void* ptr, u64 size) {
    if (mprotect(ptr, size, PROT_NONE) != 0) {
        return ARENA_PROTECTION_FAILED;
    }
    return ARENA_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ArenaError unprotect_memory_os(void* ptr, u64 size) {
    if (size > 0) {
        if (mprotect(ptr, size, PROT_READ | PROT_WRITE) != 0) {
            return ARENA_PROTECTION_FAILED;
        }
    }
    return ARENA_SUCCESS;
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File system operations

bool file_exists_os(FlString path) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);
    bool exists = access(os_path_str(os_path), F_OK) == 0;
    return exists;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_directory_exists_os(FlString path) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);
    struct stat st;
    bool exists = stat(os_path_str(os_path), &st) == 0 && S_ISDIR(st.st_mode);
    return exists;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_create_directory_os(FlString path) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);
    bool success = mkdir(os_path_str(os_path), 0755) == 0;
    return success;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_create_directory_recursive_os(FlString path) {
    if (string_is_empty(path)) {
        return false;
    }

    if (file_directory_exists_os(path)) {
        return true;
    }

    arena_scratch_auto(temp);

    char* path_copy = arena_alloc_array(temp.arena, char, path.length + 1);
    memcpy(path_copy, path.data, path.length);
    path_copy[path.length] = '\0';

    char* p = path_copy;

    if (*p == '/') {
        p++;
    }

    while (*p != '\0') {
        while (*p != '\0' && *p != '/') {
            p++;
        }

        char saved = *p;
        *p = '\0';

        // A concurrent creator can make the component between the existence check and the create
        // call; the component existing is the desired outcome, so only a create failure with the
        // directory still absent is an error.
        FlString component = (FlString) { .data = path_copy, .length = (u64)(p - path_copy) };
        if (!file_directory_exists_os(component)) {
            if (!file_create_directory_os(component) && !file_directory_exists_os(component)) {
                return false;
            }
        }

        *p = saved;
        if (*p != '\0') {
            p++;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 file_get_modification_time_os(FlString path) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);
    struct stat st;
    u64 mtime = 0;
    if (stat(os_path_str(os_path), &st) == 0) {
        mtime = (u64)st.st_mtime * 1000; // Convert to milliseconds
    }
    return mtime;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void file_delete_os(FlString path) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);
    remove(os_path_str(os_path));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 get_current_time_ms_os(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (u64)ts.tv_sec * 1000 + (u64)ts.tv_nsec / 1000000;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File system information and directory operations

FileStat file_stat_os(FlString path) {
    FileStat result = { 0 };

    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);

    struct stat st;
    if (stat(os_path_str(os_path), &st) == 0) {
        result.name = (FlString) { 0 }; // Not used for single file stat
        result.size = (u64)st.st_size;
        result.modification_time = (u64)st.st_mtime * 1000; // Convert to milliseconds
        result.is_directory = S_ISDIR(st.st_mode);
        result.is_regular_file = S_ISREG(st.st_mode);
        result.is_valid = true;
        result.mode = (u32)st.st_mode;

        // Use lstat to detect symlinks (stat follows symlinks, lstat doesn't)
        struct stat lst;
        if (lstat(os_path_str(os_path), &lst) == 0) {
            result.is_symlink = S_ISLNK(lst.st_mode);
        }
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString file_build_path_os(struct FlArena* arena, FlString base_path, FlString relative_path) {
    if (base_path.length == 0) {
        return string_copy(arena, relative_path);
    }
    if (relative_path.length == 0) {
        return string_copy(arena, base_path);
    }

    bool base_ends_with_slash = base_path.data[base_path.length - 1] == '/';
    bool relative_starts_with_slash = relative_path.data[0] == '/';

    if (base_ends_with_slash && relative_starts_with_slash) {
        FlString relative_without_slash = { .data = relative_path.data + 1, .length = relative_path.length - 1 };
        return string_concat(arena, base_path, relative_without_slash);
    } else if (!base_ends_with_slash && !relative_starts_with_slash) {
        return string_concat(arena, string_concat(arena, base_path, S("/")), relative_path);
    } else {
        return string_concat(arena, base_path, relative_path);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DirHandle dir_open_os(FlArena* arena, FlString path) {
    DirHandle handle = { 0 };

    OsPath os_path = string_to_os_path(arena, path);

    DIR* dir = opendir(os_path_str(os_path));
    if (dir) {
        DIR** dir_ptr = arena_alloc(arena, DIR*);
        *dir_ptr = dir;
        handle.platform_data = dir_ptr;
        handle.base_path = path;
        handle.is_valid = true;
    }

    return handle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileStat dir_read_next_os(FlArena* arena, DirHandle* dir_handle) {
    FileStat result = { 0 };
    result.is_valid = false;

    if (!dir_handle || !dir_handle->is_valid) {
        return result;
    }

    DIR** dir_ptr = (DIR**)dir_handle->platform_data;
    DIR* dir = *dir_ptr;
    if (!dir) {
        return result;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip . and ..
        if ((entry->d_name[0] == '.' && entry->d_name[1] == '\0')
            || (entry->d_name[0] == '.' && entry->d_name[1] == '.' && entry->d_name[2] == '\0')) {
            continue;
        }

        // The scratch must not alias the caller's arena: the copied name below lives on arena
        // and would be released by the scratch pop if they shared the same underlying arena.
        arena_scratch_auto_conflict(temp, arena);
        // Copy the name out of readdir's internal dirent: that buffer is reused/invalidated
        // by the next readdir/closedir on the same stream.
        result.name = string_copy(arena, string_from_cstr(entry->d_name));
        result.is_valid = true;

        FlString full_path = file_build_path_os(temp.arena, dir_handle->base_path, result.name);
        FileStat stat_result = file_stat_os(full_path);
        if (stat_result.is_valid) {
            result.size = stat_result.size;
            result.modification_time = stat_result.modification_time;
            result.is_directory = stat_result.is_directory;
            result.is_regular_file = stat_result.is_regular_file;
            result.is_symlink = stat_result.is_symlink;
            result.mode = stat_result.mode;
        } else {
            result.size = 0;
            result.modification_time = 0;
            result.is_directory = (entry->d_type == DT_DIR);
            result.is_regular_file = (entry->d_type == DT_REG);
            result.is_symlink = (entry->d_type == DT_LNK);
            result.mode = 0;
        }

        break; // Found valid entry
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void dir_close_os(DirHandle* dir_handle) {
    if (dir_handle && dir_handle->is_valid) {
        DIR** dir_ptr = (DIR**)dir_handle->platform_data;
        if (dir_ptr && *dir_ptr) {
            closedir(*dir_ptr);
        }
        dir_handle->is_valid = false;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_write_os(FlString path, const u8* data, i64 size) {
    arena_scratch_auto(temp);
    const char* cstr_path = string_to_cstr(temp.arena, path);

    FILE* file = fopen(cstr_path, "wb");

    if (!file) {
        return -1;
    }

    size_t bytes_written = fwrite(data, 1, (size_t)size, file);
    fclose(file);

    return (i64)bytes_written;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_remove_directory_os(FlString path) {
    arena_scratch_auto(temp);
    const char* cstr_path = string_to_cstr(temp.arena, path);
    int result = rmdir(cstr_path);
    return result == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_remove_directory_recursive_os(FlString path) {
    arena_scratch_auto(temp);

    DirHandle dir_handle = dir_open_os(temp.arena, path);
    if (!dir_handle.is_valid) {
        return false;
    }

    FileStat entry;
    while ((entry = dir_read_next_os(temp.arena, &dir_handle)).is_valid) {
        if (string_equals(entry.name, S(".")) || string_equals(entry.name, S(".."))) {
            continue;
        }

        FlString item_path = file_build_path_os(temp.arena, path, entry.name);

        if (entry.is_directory) {
            file_remove_directory_recursive_os(item_path);
        } else {
            file_delete_os(item_path);
        }
    }

    dir_close_os(&dir_handle);

    bool success = file_remove_directory_os(path);
    return success;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// System information and utilities

FlString get_temp_directory_os(struct FlArena* arena) {
    const char* temp_dirs[] = { "TMPDIR", "TMP", "TEMP", "TEMPDIR" };

    for_count(i, sizeof(temp_dirs) / sizeof(temp_dirs[0])) {
        const char* temp = getenv(temp_dirs[i]);
        if (temp && temp[0] != '\0') {
            return string_copy(arena, string_from_cstr(temp));
        }
    }

    return string_copy(arena, S("/tmp"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString get_current_directory_os(struct FlArena* arena) {
    // The buffer must come from the arena, not from getcwd's allocating GNU extension: that buffer is
    // libc malloc's, and mimalloc overrides free() here. 32KB matches Windows' extended path limit.
    const size_t initial_size = 32768;
    char* buffer = arena_alloc_array(arena, char, initial_size);

    if (getcwd(buffer, initial_size)) {
        return string_from_cstr(buffer);
    }

    const size_t huge_size = 65536;
    buffer = arena_alloc_array(arena, char, huge_size);

    if (getcwd(buffer, huge_size)) {
        return string_from_cstr(buffer);
    }

    return (FlString) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString get_executable_directory_os(struct FlArena* arena) {
    char exe_path[4096];

#if PLATFORM_LINUX
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        return (FlString) { 0 };
    }
    exe_path[len] = '\0';
#elif PLATFORM_MACOS
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        return (FlString) { 0 };
    }
    char real_path[4096];
    if (realpath(exe_path, real_path) == nullptr) {
        return (FlString) { 0 };
    }
    memory_copy(exe_path, sizeof(exe_path), real_path, strlen(real_path) + 1);
#else
    return (FlString) { 0 };
#endif

    char* last_sep = strrchr(exe_path, '/');
    if (last_sep != nullptr) {
        *last_sep = '\0';
    }

    return string_copy(arena, string_from_cstr(exe_path));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 get_process_id_os(void) {
    return (u32)getpid();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int execute_command_os(FlString command) {
    arena_scratch_auto(temp);

    char* cmd = arena_alloc_array(temp.arena, char, command.length + 1);
    memory_copy(cmd, command.length + 1, command.data, command.length);
    cmd[command.length] = '\0';

    int result = system(cmd);

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Thread management - POSIX implementation

typedef struct {
    pthread_t pthread;
#if PLATFORM_LINUX
    _Atomic(pid_t) tid; // Linux thread ID (for setpriority)
#endif
} PosixThread;

_Static_assert(sizeof(PosixThread) <= sizeof(Thread), "Thread structure too small for pthread_t");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if PLATFORM_LINUX
typedef struct {
    ThreadFunc func;
    void* user_data;
    _Atomic(pid_t)* tid_ptr;
} ThreadStartData;

static void* thread_entry_wrapper(void* arg) {
    ThreadStartData* data = (ThreadStartData*)arg;
    ThreadFunc func = data->func;
    void* user_data = data->user_data;

    atomic_store(data->tid_ptr, (pid_t)syscall(SYS_gettid));
    mi_free(data);

    return func(user_data);
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool os_thread_create(Thread* thread, ThreadFunc func, void* user_data) {
    PosixThread* pt = (PosixThread*)thread;

#if PLATFORM_LINUX
    ThreadStartData* data = mi_alloc(ThreadStartData);
    if (!data) {
        return false;
    }
    data->func = func;
    data->user_data = user_data;
    data->tid_ptr = &pt->tid;
    atomic_store(&pt->tid, 0);

    int result = pthread_create(&pt->pthread, nullptr, thread_entry_wrapper, data);
    if (result != 0) {
        mi_free(data);
        return false;
    }
    return true;
#else
    int result = pthread_create(&pt->pthread, nullptr, func, user_data);
    return result == 0;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* os_thread_join(Thread* thread) {
    PosixThread* pt = (PosixThread*)thread;
    void* ret_val = nullptr;
    pthread_join(pt->pthread, &ret_val);
    return ret_val;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void os_thread_detach(Thread* thread) {
    PosixThread* pt = (PosixThread*)thread;
    pthread_detach(pt->pthread);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 os_thread_get_current_id(void) {
    // Cast to u64 for a unique identifier (implementation-defined behavior)
    return (u64)pthread_self();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void os_thread_set_name(const char* name) {
#if PLATFORM_MACOS
    // macOS: pthread_setname_np sets the name of the calling thread (no thread parameter)
    pthread_setname_np(name);
#else
    // Linux: pthread_setname_np takes thread + name, max 16 chars including null
    pthread_setname_np(pthread_self(), name);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool os_thread_set_affinity(Thread* thread, const int* cpu_ids, int count) {
#if PLATFORM_LINUX
    if (count == 0) {
        return true;
    }

    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);

    for_count(i, count) {
        if (cpu_ids[i] >= 0 && cpu_ids[i] < CPU_SETSIZE) {
            CPU_SET(cpu_ids[i], &cpu_set);
        }
    }

    pthread_t pthread_handle;
    if (thread == nullptr) {
        pthread_handle = pthread_self();
    } else {
        PosixThread* pt = (PosixThread*)thread;
        pthread_handle = pt->pthread;
    }

    int result = pthread_setaffinity_np(pthread_handle, sizeof(cpu_set_t), &cpu_set);
    return result == 0;
#else
    // macOS doesn't support thread affinity in the same way
    (void)thread;
    (void)cpu_ids;
    (void)count;
    return true;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool os_thread_set_priority(Thread* thread, ThreadPriority priority) {
#if PLATFORM_LINUX
    int nice_value;
    switch (priority) {
        case THREAD_PRIO_LOW:
            nice_value = 10;
            break;
        case THREAD_PRIO_NORMAL:
        default:
            nice_value = 0;
            break;
    }

    pid_t tid;
    if (thread == nullptr) {
        tid = (pid_t)syscall(SYS_gettid);
    } else {
        PosixThread* pt = (PosixThread*)thread;
        tid = atomic_load(&pt->tid);

        // Wait briefly if TID not yet set (thread just started)
        int retries = 100;
        while (tid == 0 && retries-- > 0) {
            usleep(1000); // 1ms
            tid = atomic_load(&pt->tid);
        }

        if (tid == 0) {
            return false;
        }
    }

    // setpriority returns -1 on error, but -1 is also a valid nice value
    // so we need to clear errno first and check it after
    errno = 0;
    int result = setpriority(PRIO_PROCESS, tid, nice_value);
    if (result == -1 && errno != 0) {
        return false;
    }

    return true;
#else
    // macOS: not implemented yet, just return success
    (void)thread;
    (void)priority;
    return true;
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sleep functions

void sleep_us(u64 microseconds) {
    usleep((useconds_t)microseconds);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sleep_ms(u64 milliseconds) {
    // usleep() fails with EINVAL for arguments >= 1,000,000 us on macOS, turning
    // sleeps of a second or more into silent no-ops; nanosleep has no such
    // ceiling. Resume on EINTR so a signal doesn't cut the sleep short.
    struct timespec req = {
        .tv_sec = (time_t)(milliseconds / 1000),
        .tv_nsec = (long)((milliseconds % 1000) * 1000000),
    };
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Environment variables

const char* getenv_os(const char* name) {
    return getenv(name);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int setenv_os(const char* name, const char* value, int overwrite) {
    return setenv(name, value, overwrite);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int unsetenv_os(const char* name) {
    return unsetenv(name);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Test seam: forces every statvfs() lookup below to fail, so the directory walk-up can be driven
// all the way past the root path.
static bool g_force_statvfs_failure = false;

void os_disk_space_test_force_statvfs_failure(bool enable) {
    g_force_statvfs_failure = enable;
}

DiskSpaceInfo get_disk_space_os(FlString path) {
    DiskSpaceInfo result = { 0 };

    arena_scratch_auto(temp);

    char* path_copy = arena_alloc_array(temp.arena, char, path.length + 1);
    memcpy(path_copy, path.data, path.length);
    path_copy[path.length] = '\0';

    struct statvfs stat_buf;

    // Walk up the directory tree until an existing path is found: the target file may not exist yet
    while (path_copy[0] != '\0') {
        if (!g_force_statvfs_failure && statvfs(path_copy, &stat_buf) == 0) {
            u64 block_size = stat_buf.f_frsize;

            result.bytes_available = (u64)stat_buf.f_bavail * block_size;
            result.bytes_total = (u64)stat_buf.f_blocks * block_size;
            result.bytes_free = (u64)stat_buf.f_bfree * block_size;
            result.is_valid = true;
            break;
        }

        char* last_sep = strrchr(path_copy, '/');
        if (last_sep == nullptr) {
            break;
        }

        if (last_sep == path_copy) {
            if (path_copy[1] == '\0') {
                break; // Root "/" itself was just tried and failed - give up
            }
            // Truncate to root "/" for one final attempt
            path_copy[1] = '\0';
        } else {
            *last_sep = '\0';
        }
    }

    return result;
}
