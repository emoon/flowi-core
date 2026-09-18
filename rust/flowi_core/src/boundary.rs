//! The FFI-boundary kit for crates that mirror core's C ABI - plugin SDKs and ABI shims
//! that hand-write extern "C" entry points against the same layouts the C side uses.
//!
//! Everything here is raw ABI: an opaque Arena behind a raw pointer, a panic-containment
//! guard that sits inside an extern "C" body. Application code uses the crate root's
//! surface instead - [`Arena`](crate::Arena), plain &str arguments.
//!
//! - Panic containment - [`ffi_guard`](crate::boundary::ffi_guard) to wrap every
//!   extern "C" body, [`set_panic_logger`](crate::boundary::set_panic_logger) to route
//!   a caught payload at the host's log service, and
//!   [`clear_poison`](crate::boundary::clear_poison) for a host that reloads the plugin.
//! - The opaque host type - [`FlArena`](crate::boundary::FlArena), as it appears behind a
//!   raw pointer in hand-written C signatures. It keeps its C name here; the root's
//!   [`Arena`](crate::Arena) is the safe form.
//! - Raw entry points still awaiting a safe façade - the arena, jobs and perf-scope
//!   calls collected under "Raw C entry points" below, and the value types that appear in
//!   one.
//! - Marshalling conveniences - the arena copies every shim performs on its way out
//!   ([`arena_alloc_array`](crate::boundary::arena_alloc_array),
//!   [`arena_box`](crate::boundary::arena_box), the slice views).
//!
//! A layer above core adds its own boundary types to this set - flowi's `boundary` module
//! re-exports all of it and appends the VFS mount and the display rect.
//!
//! Strings are not here. The ABI string type is [`RawStr`](crate::RawStr) at the
//! crate root, and every way of building one is a constructor on it - including the arena
//! packers [`RawStr::in_arena`](crate::RawStr::in_arena) and
//! [`RawStr::in_arena_nul`](crate::RawStr::in_arena_nul).

/// Wrap the body of an extern "C" entry point: a Rust panic is caught and turned into the
/// caller-supplied error value instead of unwinding across the C ABI (which is UB). A caught
/// panic also poisons the plugin - every later guarded entry returns its error slot without
/// running.
pub use flowi_core_sys::ffi_guard;

/// Install the logger [`ffi_guard`] hands a caught panic's payload to - a plugin's export
/// shim wires this to the host log service.
pub use flowi_core_sys::set_panic_logger;

/// Clear the process-global poison [`ffi_guard`] sets on a caught panic. For a host that
/// reloads the plugin; poison is otherwise terminal for the plugin's lifetime.
pub use flowi_core_sys::clear_poison;

/// The opaque C FlArena, as it appears behind `*mut FlArena` in hand-written C signatures.
/// The crate root's [`Arena`](crate::Arena) is the safe typed allocator - prefer it anywhere the
/// raw pointer is not literally the ABI.
pub use flowi_core_sys::Arena as FlArena;

/// Compile-time proof that the whole kit is reachable through this one module, with each function
/// coerced to a fn pointer so a sys-side signature change fails here rather than at a shim.
const _: () = {
    const _GUARD: fn(u32, fn() -> u32) -> u32 = ffi_guard;
    const _SET_LOGGER: fn(fn(&str)) = set_panic_logger;
    const _CLEAR_POISON: fn() = clear_poison;
    const _ARENA: Option<*mut FlArena> = None;
};

// ---------------------------------------------------------------------------------------------------------------
// Raw C entry points
// ---------------------------------------------------------------------------------------------------------------
//
// Everything below this line is a raw `fl_*` / `vfs_*` / `arena_*` call, or a value type that only ever appears in
// one, re-exported unchanged from the sys crate. None of it is a recommended surface - reach for the crate root's
// safe form wherever one exists ([`Arena`](crate::Arena), [`Jobs`](crate::Jobs),
// [`FileWatcher`](crate::FileWatcher), the log crate).

/// Create a named arena reserving reserve_size bytes. The safe [`Arena`](crate::Arena) is the form to prefer;
/// this is for a shim that hands the raw `*mut FlArena` straight back across a C signature.
pub use flowi_core_sys::arena_create;

/// Destroy an arena from [`arena_create`], releasing its reservation.
pub use flowi_core_sys::arena_destroy;

use flowi_core_sys::arena_alloc_raw;

/// Queue a C job function with an opaque payload, returning its [`JobHandle`](crate::JobHandle). The safe
/// [`Jobs`](crate::Jobs) closure API is the form to prefer; this is for the hand-rolled trampolines that still
/// own their payload as a raw pointer.
pub use flowi_core_sys::fl_jobs_add_job;

/// As [`fl_jobs_add_job`], but the job runs only once dependency has finished.
pub use flowi_core_sys::fl_jobs_add_job_with_dependency;

/// Whether a queued job has finished, without blocking.
pub use flowi_core_sys::fl_jobs_is_finished;

/// Block until a queued job finishes.
pub use flowi_core_sys::fl_jobs_wait;

/// The worker identity a job body is handed - which worker thread is running it, and its scratch arena.
pub use flowi_core_sys::JobsWorkerInfo;

/// Open a named profiling scope, closed by [`fl_perf_scope_end`]. Unbalanced calls corrupt the profiler's stack.
pub use flowi_core_sys::fl_perf_scope_begin;

/// Close the scope opened by [`fl_perf_scope_begin`].
pub use flowi_core_sys::fl_perf_scope_end;

/// Open the per-frame profiling scope, closed by [`fl_perf_scope_frame_end`].
pub use flowi_core_sys::fl_perf_scope_frame_begin;

/// Close the per-frame profiling scope.
pub use flowi_core_sys::fl_perf_scope_frame_end;

/// The same compile-time proof the kit above gets, extended over the raw section. The value types are pinned
/// by naming them in those signatures.
const _: () = {
    const _ARENA_CREATE: unsafe extern "C" fn(u64, *const core::ffi::c_char, i32) -> *mut FlArena =
        arena_create;
    const _ARENA_DESTROY: unsafe extern "C" fn(*mut FlArena) = arena_destroy;
    const _ARENA_ALLOC_RAW: unsafe extern "C" fn(*mut FlArena, u64, u64) -> *mut core::ffi::c_void =
        arena_alloc_raw;

    const _JOBS_ADD: unsafe extern "C" fn(
        flowi_core_sys::JobsFunc,
        *mut core::ffi::c_void,
    ) -> crate::JobHandle = fl_jobs_add_job;
    const _JOBS_ADD_DEP: unsafe extern "C" fn(
        flowi_core_sys::JobsFunc,
        *mut core::ffi::c_void,
        crate::JobHandle,
    ) -> crate::JobHandle = fl_jobs_add_job_with_dependency;
    const _JOBS_IS_FINISHED: unsafe extern "C" fn(crate::JobHandle) -> crate::JobsResult =
        fl_jobs_is_finished;
    const _JOBS_WAIT: unsafe extern "C" fn(crate::JobHandle) = fl_jobs_wait;
    const _JOBS_WORKER_INFO: Option<JobsWorkerInfo> = None;

    const _PERF_BEGIN: unsafe extern "C" fn(*const core::ffi::c_char) -> u32 = fl_perf_scope_begin;
    const _PERF_END: unsafe extern "C" fn(u32) = fl_perf_scope_end;
    const _PERF_FRAME_BEGIN: unsafe extern "C" fn() = fl_perf_scope_frame_begin;
    const _PERF_FRAME_END: unsafe extern "C" fn() = fl_perf_scope_frame_end;
};

// ---------------------------------------------------------------------------------------------------------------
// Marshalling conveniences
// ---------------------------------------------------------------------------------------------------------------
//
// The arena copies a C-facing shim performs on its way out. The arena entries are unsafe for one shared
// reason: the caller supplies a live `*mut FlArena` for the call and nothing checks it.

/// Allocate count uninitialised T out of arena - the array a result struct hands back by pointer.
///
/// Panics on a count whose byte size does not fit a usize, rather than allocating the wrapped
/// remainder and letting the caller write count elements into it.
///
/// # Safety
/// arena must be a live arena for the call, and the caller must initialise every element it lets C read.
pub unsafe fn arena_alloc_array<T>(arena: *mut FlArena, count: usize) -> *mut T {
    let size = count
        .checked_mul(core::mem::size_of::<T>())
        .expect("arena array size overflow");
    // SAFETY: arena is live for the call - the caller's contract.
    unsafe { arena_alloc_raw(arena, size as u64, core::mem::align_of::<T>() as u64) as *mut T }
}

/// Move one T into arena and return the pointer C holds it by. The value's destructor never runs - arena
/// contents are abandoned, not dropped.
///
/// # Safety
/// arena must be a live arena for the call.
pub unsafe fn arena_box<T>(arena: *mut FlArena, value: T) -> *mut T {
    // SAFETY: arena is live for the call (the caller's contract), so the allocation returns one aligned,
    // uniquely-owned T slot; write moves value in without dropping the uninitialised destination.
    unsafe {
        let p = arena_alloc_array::<T>(arena, 1);
        p.write(value);
        p
    }
}

/// View a C (pointer, count) pair as a slice - the shape every array argument takes across the ABI.
///
/// A null pointer or a zero count yields an empty slice rather than an invalid one. C spells "no elements"
/// both ways and from_raw_parts accepts neither: a null pointer is instant UB even at length zero, and a
/// dangling-but-aligned pointer is what the caller would otherwise have to invent.
///
/// # Safety
/// When items is non-null and count is non-zero, it must point at count initialised, contiguous
/// T that stay put and unwritten for 'a. The caller picks 'a, so it owes that lifetime.
#[inline]
pub unsafe fn borrow_slice<'a, T>(items: *const T, count: usize) -> &'a [T] {
    if items.is_null() || count == 0 {
        return &[];
    }
    // SAFETY: the caller's contract, minus the null / empty cases handled above.
    unsafe { core::slice::from_raw_parts(items, count) }
}

/// [`borrow_slice`] for a buffer C hands over to be written - an output array or a mix destination.
///
/// # Safety
/// When items is non-null and count is non-zero, it must point at count contiguous, writable,
/// initialised T that no one else touches for 'a. The caller picks 'a, so it owes that lifetime.
#[inline]
pub unsafe fn borrow_slice_mut<'a, T>(items: *mut T, count: usize) -> &'a mut [T] {
    if items.is_null() || count == 0 {
        return &mut [];
    }
    // SAFETY: the caller's contract, minus the null / empty cases handled above.
    unsafe { core::slice::from_raw_parts_mut(items, count) }
}

#[cfg(test)]
mod tests {
    use super::{borrow_slice, borrow_slice_mut};
    use crate::RawStr;

    /// Both spellings of "no elements" a C caller may pass; from_raw_parts accepts neither.
    #[test]
    fn an_absent_array_borrows_as_empty_however_c_spelled_it() {
        // SAFETY: the null and zero-count arms never dereference anything.
        unsafe {
            assert!(borrow_slice::<u32>(core::ptr::null(), 7).is_empty());
            assert!(borrow_slice::<u32>(core::ptr::null(), 0).is_empty());
            assert!(borrow_slice_mut::<u32>(core::ptr::null_mut(), 7).is_empty());
        }

        // A non-null pointer with a zero count is the third spelling, and it must not
        // reach from_raw_parts either.
        let items = [1u32, 2, 3];
        // SAFETY: the pointer is live; the zero count is the case under test.
        assert!(unsafe { borrow_slice(items.as_ptr(), 0) }.is_empty());
    }

    #[test]
    fn a_present_array_borrows_its_elements() {
        let items = [1u32, 2, 3];
        // SAFETY: items outlives the borrow and holds exactly three initialised u32.
        assert_eq!(unsafe { borrow_slice(items.as_ptr(), items.len()) }, &items);

        let mut out = [0u32; 3];
        // SAFETY: out outlives the borrow and holds exactly three writable u32.
        unsafe { borrow_slice_mut(out.as_mut_ptr(), out.len()) }.copy_from_slice(&items);
        assert_eq!(out, items);
    }

    #[test]
    fn static_literal_packs_in_const_context() {
        const NAME: RawStr = RawStr::from_static("my.plugin");
        assert_eq!(NAME.packed >> 62 & 1, 1, "is_static must be set");
        assert_eq!(NAME.into_string(), "my.plugin");
    }

    #[test]
    fn borrow_round_trips_through_owned() {
        assert_eq!(
            (RawStr::borrow("host said this")).into_string(),
            "host said this"
        );
    }

    mod marshalling {
        use super::super::{arena_alloc_array, arena_box};
        use crate::Arena;
        use crate::RawStr;

        const RESERVE: u64 = 1 << 16;

        /// The bytes behind an arena-packed RawStr, terminator excluded.
        fn packed_bytes(s: RawStr) -> Vec<u8> {
            // SAFETY: the callers below just packed s into a live arena.
            unsafe { core::slice::from_raw_parts(s.data as *const u8, s.len()).to_vec() }
        }

        #[test]
        fn empty_sentinel_is_null_and_zero_length() {
            assert!(RawStr::EMPTY.data.is_null());
            assert!(RawStr::EMPTY.is_empty());
            assert_eq!(RawStr::EMPTY.len(), 0);
        }

        /// The flag bits sit above the length, so a string is never mistaken for a longer one.
        #[test]
        fn length_ignores_the_flag_bits() {
            let all_flags = RawStr {
                data: core::ptr::null(),
                packed: 5 | (1u64 << 62) | (1u64 << 63),
            };
            assert_eq!(all_flags.len(), 5);
        }

        #[test]
        fn arena_str_copies_the_bytes_and_reports_their_length() {
            let arena = Arena::new(RESERVE);
            // SAFETY: arena is live for the call.
            let packed = unsafe { RawStr::in_arena(arena.as_raw(), "content/disk2.adf") };
            assert_eq!(packed.len(), 17);
            assert_eq!(packed.into_string(), "content/disk2.adf");
        }

        #[test]
        fn arena_str_maps_the_empty_string_to_the_sentinel() {
            let arena = Arena::new(RESERVE);
            // SAFETY: arena is live for the call.
            let packed = unsafe { RawStr::in_arena(arena.as_raw(), "") };
            assert!(packed.data.is_null());
            assert_eq!(packed.packed, 0);
        }

        /// is_ascii is set from the content, the meaning C attaches to the bit - never
        /// unconditionally, or C's fast-path codepoint walk would misread UTF-8.
        #[test]
        fn arena_str_sets_the_ascii_bit_from_the_content() {
            let arena = Arena::new(RESERVE);
            // SAFETY: arena is live for both calls.
            let (ascii, wide) = unsafe {
                (
                    RawStr::in_arena(arena.as_raw(), "plain"),
                    RawStr::in_arena(arena.as_raw(), "Bjørn"),
                )
            };
            assert_eq!(ascii.packed >> 63, 1);
            assert_eq!(wide.packed >> 63, 0);
            // Arena copies are not 'static, so the bit below is_ascii stays clear.
            assert_eq!(ascii.packed >> 62 & 1, 0);
        }

        /// The terminator is written for the C reader but stays out of the length, so a
        /// %S and a %s of the same value agree.
        #[test]
        fn arena_str_nul_terminates_without_counting_the_terminator() {
            let arena = Arena::new(RESERVE);
            // SAFETY: arena is live for the call, and the packer wrote len + 1 bytes.
            let packed = unsafe { RawStr::in_arena_nul(arena.as_raw(), "hvsc") };
            assert_eq!(packed.len(), 4);
            assert_eq!(packed_bytes(packed), b"hvsc");
            // SAFETY: as above - the byte one past the reported length is the terminator.
            assert_eq!(unsafe { *packed.data.add(4) }, 0);
        }

        #[test]
        fn arena_str_nul_maps_the_empty_string_to_the_sentinel() {
            let arena = Arena::new(RESERVE);
            // SAFETY: arena is live for the call.
            let packed = unsafe { RawStr::in_arena_nul(arena.as_raw(), "") };
            assert!(packed.data.is_null());
            assert_eq!(packed.packed, 0);
        }

        #[test]
        fn arena_alloc_array_hands_back_aligned_writable_slots() {
            let arena = Arena::new(RESERVE);
            // SAFETY: arena is live; every one of the three slots is written before it
            // is read back.
            let values = unsafe {
                let slots = arena_alloc_array::<u64>(arena.as_raw(), 3);
                assert_eq!(slots.align_offset(core::mem::align_of::<u64>()), 0);
                for (i, value) in [7u64, 8, 9].into_iter().enumerate() {
                    slots.add(i).write(value);
                }
                core::slice::from_raw_parts(slots, 3).to_vec()
            };
            assert_eq!(values, [7, 8, 9]);
        }

        /// Same contract as the C array macros: an overflowing count refuses rather than handing
        /// back the wrapped remainder for the caller to overrun.
        #[test]
        #[should_panic(expected = "arena array size overflow")]
        fn arena_alloc_array_refuses_a_count_whose_byte_size_overflows() {
            let arena = Arena::new(RESERVE);
            // SAFETY: arena is live; the multiply panics before the allocation is attempted.
            unsafe { arena_alloc_array::<u64>(arena.as_raw(), usize::MAX / 4 + 1) };
        }

        #[test]
        fn arena_box_moves_the_value_into_the_arena() {
            let arena = Arena::new(RESERVE);
            // SAFETY: arena is live and arena_box initialised the slot it returns.
            let read_back = unsafe { *arena_box(arena.as_raw(), (1u32, 2u32)) };
            assert_eq!(read_back, (1, 2));
        }
    }
}
