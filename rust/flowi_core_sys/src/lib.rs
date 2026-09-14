//! Raw FFI for flowi-core: the C foundation's ABI, exactly as C declares it.
//!
//! The api_gen-authored bindings live in [`generated`], single-sourced from the
//! `api/*.def` IDL beside them: api_gen emits both the Rust binding here and the real
//! `Fl*` C typedef in `include/flowi/<module>/*.h`. The Rust spelling drops the C prefix
//! (FlColor -> Color). [`RawStr`] is the one hand-packed type, mirroring FlString, whose
//! bitfield word has no exported constructor to bind.
//!
//! Everything here is unsafe by nature. The safe, borrow-checked surface over the same
//! ABI is the `flowi_core` crate.

#![allow(non_camel_case_types)]

use std::os::raw::c_char;

mod generated;

#[cfg(feature = "allocation-probe")]
pub mod allocation_probe;
pub mod allocator;

#[cfg(feature = "global-allocator")]
#[global_allocator]
static ALLOCATOR: allocator::HeapAllocator = allocator::HeapAllocator;

/// Panic containment for cdylibs: every extern "C" entry a plugin exports wraps its body
/// in [`ffi_guard`] so a Rust panic never unwinds across the C ABI - it is logged,
/// poisons the plugin, and returns the slot's error value instead. See [`guard`] for the
/// full contract. A direct-link executable does not use this; its frame trampoline aborts
/// on panic instead.
pub mod guard;
pub use guard::{clear_poison, ffi_guard, is_poisoned, poison, set_panic_logger};

// The generated modules name each other's types by crate-root path (`crate::Arena`), so
// every one of them is re-exported flat here. That is also the surface a dependent crate
// re-exports to keep those paths resolving on its own root.

/// The `arena_*` call API (flowi/arena/arena.h): the opaque [`Arena`] (an uninhabited enum,
/// crossed only behind `*mut Arena`), the [`TempArena`] savepoint a temp/scratch scope
/// restores from, the allocate/rewind/scratch entry points, and the process-wide `fl_init` /
/// `fl_destroy` foundation lifecycle. The callable surface is the `flowi_core` crate's
/// typed, borrow-checked `Arena`.
pub use generated::arena::*;
/// The public file-watcher surface (flowi/core/file_watcher.h): the FlFileChangeType
/// bitflags, the watch config, the change record and the arena-allocated batch
/// fl_file_watcher_take_changes hands back, plus the `fl_file_watcher_*` entry points. This
/// counted-array shape is what crosses the library boundary; core's own C code walks changes
/// through the iterator in src/core/file_watcher.h instead.
pub use generated::file_watcher::*;
/// The `fl_heap_*` call API (flowi/heap/heap.h): the process heap C and Rust share. Backs
/// [`allocator::HeapAllocator`], so a Rust allocation and a C one come from the same place.
pub use generated::heap::*;
/// The `fl_jobs_*` call API (flowi/core/jobs.h): the JobHandle handle, the JobsWorkerInfo
/// POD, the JobsFunc callback alias, the JobsResult / JobPriority enums, and the `fl_jobs_*`
/// functions. Arena crosses behind `*mut Arena`. The callable surface is the `flowi_core`
/// crate's safe Jobs façade.
pub use generated::jobs::*;
/// The `fl_json_*` call API (flowi/core/json.h): the JSON DOM structs (the recursive
/// JsonValue / JsonObjectElement / JsonArrayElement cluster plus JsonObject / JsonArray /
/// JsonNumber / JsonParseResult), the JsonType / JsonParseError enums, and the `fl_json_*`
/// functions. Arena crosses behind `*mut Arena` and FlString as [`RawStr`]. The
/// FlJsonParseFlags bitset enum is a hand-written carve-out (seam header only).
pub use generated::json::*;
/// The `fl_log_*` call API (flowi/core/log.h): the LogLevel severity enum plus the channel
/// register/level/enable, file-logging and set-level/flush functions. The variadic
/// fl_log_c_message / fl_log_message and the va_list fl_log_c_vmessage_loc carry no Rust
/// binding - varargs do not cross the FFI, so the `flowi_core` crate's log-crate bridge
/// formats Rust-side and emits through fl_log_c_message_formatted.
pub use generated::log::*;
/// The shared value types (flowi/core/math_data.h): [`Color`] and [`Vec2`]. They are core's
/// because everything above core embeds them by value.
pub use generated::math_data::*;
/// The `fl_memory_tracker_*` call API (flowi/core/memory_tracker.h): the POD MemoryStats
/// snapshot (arena usage + the mimalloc alloc/free balance) plus the
/// init/destroy/dump_*/export_json_*/get_stats functions.
pub use generated::memory_tracker::*;
/// The `path_*` manipulation functions (flowi/path/path.h). The `flowi_core` crate's `path`
/// module wraps the three a caller reaches for (join, parent, absoluteness) with owned String
/// results and a scratch arena of its own; the rest are here for a shim that already has an
/// arena in hand.
pub use generated::path::*;
/// The always-on frame-profiler call surface (`fl_perf_scope_*`, flowi/core/perf_scope.h): a
/// host that owns the frame loop brackets it with frame_begin/frame_end and opens named
/// scopes inside.
pub use generated::perf_scope::*;
/// The status vocabulary (flowi/core/status.h): the general-purpose Status code and
/// [`FlError`], the one shared error enum the fallible Rust surfaces return and plugins use
/// directly.
pub use generated::status::*;
/// The `string_*` / `sb_*` call API (flowi/string/string.h): the StringBuilder, the
/// parse-result PODs and StringParseStatus, and the comparison, search and hashing functions
/// over [`RawStr`]. natural-order comparison and the FNV-1a hash encode rules the C side must
/// agree with byte for byte - an ordering, and a hash whose value gets written down.
pub use generated::string::*;

/// Mirror of FlString (core/string.h) - a non-owning counted view of bytes crossing the
/// C ABI:
///
/// ```c
/// typedef struct FlString {
///     const char* data;
///     uint64_t length : 62;
///     uint64_t is_static : 1;
///     uint64_t is_ascii : 1;
/// } FlString;
/// ```
///
/// The type claims nothing about how long the bytes live. Four different lifetimes cross as
/// one of these, and which one you hold comes from the constructor that built it:
/// [`RawStr::from_static`] ('static), [`RawStr::borrow`] (the
/// single call it is passed to), [`RawStr::in_arena`] or [`fl_frame_str`] (the arena's - and
/// for the frame arena that is a runtime assert on the C side, not a Rust lifetime), or a host
/// return (whatever the host's contract says). Nothing in the type tracks this, so storing one
/// in a struct field is a lifetime obligation taken on by hand.
///
/// No exported constructor exists (S()/string_from_cstr_len are inline), so we mirror the
/// trailing bitfield word as a plain u64 and pack it ourselves. Bit order - length in the
/// low 62 bits - is the well-defined GCC/Clang bitfield ABI on little-endian x86-64 /
/// aarch64.
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct RawStr {
    pub data: *const c_char,
    pub packed: u64,
}

impl RawStr {
    /// The string_empty() sentinel: null data, zero length. What an absent string is on the
    /// wire - the arena constructors return it for "".
    pub const EMPTY: RawStr = RawStr {
        data: core::ptr::null(),
        packed: 0,
    };

    /// The length occupies the low 62 bits of packed; the two high bits (is_static,
    /// is_ascii) are flags and never part of it.
    const LEN_MASK: u64 = (1u64 << 62) - 1;

    /// Pack a 'static Rust string (is_static = 1). is_ascii is set only when the bytes
    /// are actually ASCII, matching the meaning the C side attaches to the bit (fast-path
    /// codepoint iteration).
    ///
    /// const, so a shim crate can name a literal's RawStr in a const item:
    ///
    /// ```ignore
    /// const NAME: RawStr = RawStr::from_static("my.plugin");
    /// ```
    pub const fn from_static(s: &'static str) -> RawStr {
        let len = s.len() as u64;
        debug_assert!(len < (1u64 << 62), "RawStr length exceeds 62 bits");
        let is_ascii = if s.is_ascii() { 1u64 } else { 0 };
        RawStr {
            data: s.as_ptr() as *const c_char,
            packed: len | (1u64 << 62) | (is_ascii << 63),
        }
    }

    /// Borrow a &str for the duration of a single call (is_static = 0): the view aliases
    /// s's bytes and is valid only while s outlives the one FFI call it is handed to.
    pub fn borrow(s: &str) -> RawStr {
        RawStr::borrow_bytes(s.as_bytes())
    }

    /// [`RawStr::borrow`] for a byte string that is not known to be UTF-8. FlString is a
    /// counted view of bytes, not a Rust str, so the byte form is the primitive one and the
    /// &str form defers to it.
    pub fn borrow_bytes(s: &[u8]) -> RawStr {
        let len = s.len() as u64;
        debug_assert!(len < (1u64 << 62), "RawStr length exceeds 62 bits");
        let is_ascii = if s.is_ascii() { 1u64 } else { 0 };
        RawStr {
            data: s.as_ptr() as *const c_char,
            packed: len | (is_ascii << 63),
        }
    }

    /// The number of bytes the view carries.
    pub const fn len(self) -> usize {
        (self.packed & RawStr::LEN_MASK) as usize
    }

    /// Whether the view carries no bytes. A null view and a zero-length one both count.
    pub const fn is_empty(self) -> bool {
        self.len() == 0
    }

    /// Copy a host-returned view into an owned String. A null data or zero length yields
    /// the empty string; non-UTF-8 bytes are replaced (lossy) so the conversion is total - it
    /// never panics.
    ///
    /// The host contract is that data..data + length is initialised and outlives this copy.
    pub fn into_string(self) -> String {
        if self.data.is_null() || self.is_empty() {
            return String::new();
        }
        // SAFETY: host contract above - data points at len() initialised bytes for the
        // duration of this call.
        let bytes = unsafe { core::slice::from_raw_parts(self.data as *const u8, self.len()) };
        String::from_utf8_lossy(bytes).into_owned()
    }

    /// Borrow a host-returned view as a &str, no copy. The counterpart to
    /// [`RawStr::into_string`] for a string the host keeps alive on the caller's behalf - a
    /// mount's source path, say, which lives as long as the mount.
    ///
    /// A null data, a zero length, or non-UTF-8 bytes all yield "", so like
    /// [`RawStr::into_string`] this is total and never panics. Callers that must distinguish
    /// "empty" from "not valid UTF-8" want the raw bytes instead.
    ///
    /// # Safety
    /// data..data + length must be initialised and stay valid and unmodified for the whole
    /// of 'a. The caller picks 'a, so it must be tied to something that actually owns the
    /// bytes (the mount, the arena) - never left to be inferred.
    pub unsafe fn as_str<'a>(&self) -> &'a str {
        if self.data.is_null() || self.is_empty() {
            return "";
        }
        // SAFETY: the caller's contract - data..data + length is initialised and stays valid and unmodified
        // for the whole of 'a; the null/zero cases returned above.
        let bytes = unsafe { core::slice::from_raw_parts(self.data as *const u8, self.len()) };
        core::str::from_utf8(bytes).unwrap_or("")
    }

    /// Copy s into arena and pack the copy. "" maps to [`RawStr::EMPTY`].
    ///
    /// is_static stays 0 - arena bytes live only as long as the arena - and is_ascii
    /// reflects the content, which is the meaning C attaches to the bit.
    ///
    /// The bytes are not NUL-terminated: use [`RawStr::in_arena_nul`] wherever the C side
    /// reads them as a C string.
    ///
    /// # Safety
    /// arena must be a live arena for the call, and nothing checks that it is.
    pub unsafe fn in_arena(arena: *mut Arena, s: &str) -> RawStr {
        if s.is_empty() {
            return RawStr::EMPTY;
        }
        // SAFETY: arena is live for the call (the caller's contract), so the allocation returns s.len()
        // writable, uniquely-owned bytes; s is a live &str and the two regions cannot overlap.
        unsafe {
            let buf = arena_alloc_raw(arena, s.len() as u64, 1) as *mut u8;
            core::ptr::copy_nonoverlapping(s.as_ptr(), buf, s.len());
            RawStr::packed_in_arena(buf as *const c_char, s)
        }
    }

    /// Copy s into arena NUL-terminated, the way C's string_copy(arena, ...) does: the
    /// terminator is written but excluded from the reported length, so the receiving C code can
    /// hand data to a %s or any `const char*` API. "" maps to [`RawStr::EMPTY`].
    ///
    /// # Safety
    /// arena must be a live arena for the call, and nothing checks that it is.
    pub unsafe fn in_arena_nul(arena: *mut Arena, s: &str) -> RawStr {
        if s.is_empty() {
            return RawStr::EMPTY;
        }
        // SAFETY: arena is live for the call (the caller's contract), so the allocation returns
        // s.len() + 1 writable, uniquely-owned bytes; s is a live &str that cannot overlap them, and
        // the terminator is written at the last of the requested bytes - in bounds.
        unsafe {
            let buf = arena_alloc_raw(arena, (s.len() + 1) as u64, 1) as *mut u8;
            core::ptr::copy_nonoverlapping(s.as_ptr(), buf, s.len());
            *buf.add(s.len()) = 0;
            RawStr::packed_in_arena(buf as *const c_char, s)
        }
    }

    /// Pack the (data, len) of a copy just made into an arena.
    fn packed_in_arena(data: *const c_char, s: &str) -> RawStr {
        RawStr {
            data,
            packed: (s.len() as u64) | ((s.is_ascii() as u64) << 63),
        }
    }
}
