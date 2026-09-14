cmake_minimum_required(VERSION 3.16)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(TargetConfig)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(flowi_core C)

# An embedder adds this with add_subdirectory and links `flowi_core`. The name is also
# the artifact name: libflowi_core.so, not the libcore.so the target was called inside
# flowi, which is too generic to put on a library search path.

# Both default off: a consumer that wants mimalloc or Tracy opts in and supplies the
# dependency. flowi turns FLOWI_CORE_USE_MIMALLOC on to keep its current allocator.
option(FLOWI_CORE_USE_MIMALLOC "Allocate through mimalloc instead of the C library" OFF)
option(FLOWI_CORE_ENABLE_PROFILING "Emit Tracy zones from the profile_* macros" OFF)
option(FLOWI_CORE_SHARED "Build core as a shared library" OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(EXTERNAL ${ROOT}/external)
set(SRC ${ROOT}/src)
set(CORE ${SRC}/core)
set(FLOWI_MATH_LIBRARY "")
if(NOT WIN32)
    set(FLOWI_MATH_LIBRARY m)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # The x86 baseline is SSSE3, not SSE3: simd.h builds i16x8_lerp and the table
    # shuffles on _mm_mulhrs_epi16 and _mm_shuffle_epi8. No TU here calls them today,
    # so the omission has been latent, but the header is ours to keep compilable.
    # Not optional and not probed - the code does not compile without it.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|i386|AMD64")
        set(SIMD_FLAGS -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(SIMD_FLAGS -march=armv8.2-a+fp16+simd)
    endif()

    # add_compile_options, not CMAKE_C_FLAGS: the flags have to arrive per target.
    # CMAKE_C_FLAGS reaches the GNU drivers but not targets under the Visual Studio
    # generator, which is how flowi spent three weeks compiling its renderer without
    # ssse3. Inside flowi this block is redundant - flowi sets the same options before
    # adding this directory - but a standalone core build on Windows has only this one.
    add_compile_options(${SIMD_FLAGS})
    list(JOIN SIMD_FLAGS " " SIMD_FLAGS_TEXT)
    message(STATUS "flowi-core SIMD: '${SIMD_FLAGS_TEXT}' (processor '${CMAKE_SYSTEM_PROCESSOR}', compiler ${CMAKE_C_COMPILER_ID})")

    if(MSVC)
        set(WARNING_FLAGS "/W3")
    else()
        set(WARNING_FLAGS "-Wall")
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        list(APPEND WARNING_FLAGS "-Wno-initializer-overrides")
    endif()
endif()

find_package(ZLIB REQUIRED)

# An embedding project may already carry these; adding them twice is a hard CMake
# error, so defer to whatever it created.
if(NOT TARGET xxhash)
    add_subdirectory(${EXTERNAL}/xxhash ${CMAKE_CURRENT_BINARY_DIR}/_dep/xxhash)
endif()
if(NOT TARGET fzy)
    add_subdirectory(${EXTERNAL}/fzy ${CMAKE_CURRENT_BINARY_DIR}/_dep/fzy)
endif()

if(FLOWI_CORE_USE_MIMALLOC AND NOT TARGET mimalloc-static)
    set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
    if(APPLE OR WIN32 OR ENABLE_ASAN OR ENABLE_TSAN OR
       "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_DEBUG} ${CMAKE_C_FLAGS_RELEASE} ${CMAKE_C_FLAGS_RELWITHDEBINFO}" MATCHES "-fsanitize=[^ ]*(address|thread)")
        set(MI_OVERRIDE OFF CACHE BOOL "Override the C malloc family" FORCE)
    else()
        set(MI_OVERRIDE ON CACHE BOOL "Override the C malloc family")
    endif()
    add_subdirectory(${EXTERNAL}/mimalloc ${CMAKE_CURRENT_BINARY_DIR}/_dep/mimalloc)
    target_compile_definitions(mimalloc-static PRIVATE MI_STAT=1)
endif()

################################################################################

# All three watcher backends compile everywhere; each guards itself on the platform macro.
# The perf_counters_* backends are #included by perf_counters.c, so they are not listed.
set(PLATFORM_SOURCES
    ${CORE}/os/file_watcher_linux.c ${CORE}/os/file_watcher_macos.c
    ${CORE}/os/file_watcher_windows.c)
if(WIN32)
    list(APPEND PLATFORM_SOURCES ${CORE}/os/windows.c)
else()
    list(APPEND PLATFORM_SOURCES ${CORE}/os/posix.c)
endif()

if(FLOWI_CORE_SHARED)
    set(FLOWI_CORE_TYPE SHARED)
else()
    set(FLOWI_CORE_TYPE STATIC)
endif()

add_library(flowi_core ${FLOWI_CORE_TYPE}
    ${CORE}/jobsys.c
    ${CORE}/arena.c
    ${CORE}/fixed_array.c
    ${CORE}/core.c
    ${CORE}/math.c
    ${CORE}/string.c
    ${CORE}/env_expand.c
    ${CORE}/memory_api.c
    ${CORE}/file_watcher.c
    ${CORE}/hash.c
    ${CORE}/hashmap.c
    ${CORE}/json.c
    ${CORE}/json_delta.c
    ${CORE}/json_builder.c
    ${CORE}/log.c
    ${CORE}/path.c
    ${CORE}/pool_allocator.c
    ${CORE}/simd.c
    ${CORE}/sprintf.c
    ${CORE}/stats.c
    ${CORE}/utf8.c
    ${CORE}/string_allocator.c
    ${CORE}/tlsf.c
    ${CORE}/perf_counters.c
    ${CORE}/perf_scope.c
    ${CORE}/cpu_topology.c
    ${CORE}/assert.c
    ${CORE}/error_report.c
    ${CORE}/memory_tracker.c
    ${CORE}/arena_watchdog.c
    ${CORE}/arena_alloc_tracker.c
    ${CORE}/crash_reporter.c
    ${CORE}/crash_detector.c
    ${CORE}/crash_handler.c
    ${CORE}/app_identity.c
    ${PLATFORM_SOURCES})

target_compile_definitions(flowi_core PRIVATE FL_API_IMPLEMENTATION=1)

if(FLOWI_CORE_USE_MIMALLOC)
    target_link_libraries(flowi_core mimalloc-static)
else()
    # memory.h leaves USE_MIMALLOC undefined at 1 by default, so the off case must say so.
    # PUBLIC: every consumer's translation units read memory.h and must agree.
    target_compile_definitions(flowi_core PUBLIC USE_MIMALLOC=0)
endif()

if(FLOWI_CORE_ENABLE_PROFILING)
    target_compile_definitions(flowi_core PUBLIC ENABLE_PROFILING=1)
endif()

if(FLOWI_CORE_SHARED)
    set_target_properties(flowi_core PROPERTIES VERSION 1.0.0 SOVERSION 1)
    if(WIN32)
        target_compile_definitions(flowi_core PUBLIC BASE_STATIC=1 FL_API=)
        set_target_properties(flowi_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()
endif()

# ${SRC} resolves the <core/x.h> spelling a few headers use; ${ROOT}/include resolves the
# api_gen-emitted <flowi/<module>/x.h> public surface.
target_include_directories(flowi_core PUBLIC ${SRC} ${ROOT} ${ROOT}/include ${EXTERNAL})
target_link_libraries(flowi_core xxhash fzy ZLIB::ZLIB ${FLOWI_MATH_LIBRARY})
target_include_directories(flowi_core SYSTEM PRIVATE ${ZLIB_INCLUDE_DIRS})

if(APPLE)
    target_link_libraries(flowi_core "-framework CoreServices" "-framework CoreFoundation")
endif()

apply_target_config(flowi_core)
