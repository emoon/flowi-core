//! Safe Rust over flowi-core: the C foundation the toolkit and its plugins are built on.
//!
//! Core is the process-wide foundation - an arena allocator, a job system, logging, path
//! and string handling, a file watcher - and nothing above it. It owns no window, no
//! renderer and no filesystem abstraction.
//!
//! Everything here is a borrow-checked wrapper over `flowi_core_sys`, which holds the raw
//! ABI and all the `unsafe`. The one process-wide obligation is the foundation itself:
//! [`init`] brings it up and [`shutdown`] takes it down, and every surface that needs it
//! brings it up for itself, so a caller reaching core only through [`Arena`] or [`Jobs`]
//! never calls either.

use std::sync::atomic::{AtomicPtr, Ordering};
use std::sync::Mutex;

use flowi_core_sys as sys;

#[cfg(feature = "allocation-probe")]
pub use flowi_core_sys::allocation_probe;
pub use flowi_core_sys::allocator::HeapAllocator;

/// The ABI string type: a non-owning counted view of bytes, mirroring C's FlString. The
/// arena packers ([`RawStr::in_arena`], [`RawStr::in_arena_nul`]) are the way to hand a
/// Rust `&str` to a C signature that keeps it.
pub use flowi_core_sys::RawStr;

/// The one shared error enum - the Err arm of the `Result<T, FlError>` a fallible slot
/// returns.
pub use flowi_core_sys::FlError;

/// Log severity, re-exported for direct use with the log control surface. Rust records
/// carry a `log::Level` instead; the bridge maps it onto this enum.
pub use flowi_core_sys::LogLevel;

/// The shared value types every layer above core embeds by value.
pub use flowi_core_sys::{Color, Vec2};

/// The typed, borrow-checked arena: [`Arena::alloc`] returns a `&mut T` tied to the borrow,
/// so [`Arena::rewind`] is a borrow error while an allocation is live. See the module docs.
mod arena;
pub use arena::{Arena, ArenaString, ArenaVec};

/// The job system: a closure-taking façade over the C worker pool, with priorities and
/// dependencies between queued jobs.
mod jobs;
pub use jobs::{JobHandle, JobPriority, Jobs, JobsResult, WorkerInfo};

/// Path manipulation - join, parent, absoluteness - with owned `String` results.
pub mod path;

/// Native file-system change notification: start a watch, drain the batch it accumulated.
mod file_watcher;
pub use file_watcher::{FileChange, FileChanges, FileWatcher, WatchConfig};
pub use flowi_core_sys::FileChangeType;

/// The bridge from the `log` crate to core's channel logging, and the log-file control
/// surface. Installed automatically when the foundation comes up.
mod log_bridge;
pub use log_bridge::install_log_bridge;
pub use log_bridge::set_target_level;
pub use log_bridge::LogFile;

/// The FFI-boundary kit for crates that hand-write extern "C" entry points against core's
/// C ABI. See the module docs.
pub mod boundary;

/// The one live main arena `fl_init` returned, or null when the foundation is down.
/// Doubles as the "is it up?" flag, guarded by [`BASE_LOCK`] for the check-then-init.
static BASE_ARENA: AtomicPtr<sys::Arena> = AtomicPtr::new(core::ptr::null_mut());
static BASE_LOCK: Mutex<()> = Mutex::new(());

/// C `fl_init` is idempotent but not thread-safe, so bring-up is serialised here. Every
/// entry point that needs the foundation comes through here, so whichever runs first brings
/// it up and the rest reuse its arena.
///
/// `job_threads` is honoured only by the call that actually brings the foundation up; a
/// later caller asking for a different count gets the live one.
///
/// Not a Once: [`shutdown`] can take the foundation back down, and a Once that stayed
/// "done" across that would leave every later constructor running against a torn-down core.
pub fn init_shared(job_threads: i32) -> *mut sys::Arena {
    // Fast path, so an already-up check costs no global mutex. The acquire load pairs with
    // the release store below.
    let live = BASE_ARENA.load(Ordering::Acquire);
    if !live.is_null() {
        return live;
    }

    // Before anything else, so a record emitted during bring-up already has somewhere to go.
    install_log_bridge();

    let _guard = BASE_LOCK.lock().unwrap_or_else(|e| e.into_inner());

    // Re-check under the lock: two threads can both miss the fast path.
    let live = BASE_ARENA.load(Ordering::Acquire);
    if !live.is_null() {
        return live;
    }

    // SAFETY: the one-time foundation bring-up, serialised by BASE_LOCK.
    let raw = unsafe { sys::fl_init(job_threads) };
    BASE_ARENA.store(raw, Ordering::Release);
    raw
}

/// Whether the foundation is up, as a single atomic load - no lock, no bring-up.
///
/// For the surfaces that must not bootstrap on their own: a job scheduled onto a foundation
/// that was never started would dereference a null global inside C, so those call sites
/// assert on this instead, turning the segfault into a diagnosable panic.
#[inline]
pub fn is_up() -> bool {
    !BASE_ARENA.load(Ordering::Acquire).is_null()
}

/// Borrow the foundation's main arena.
///
/// Panics on a null arena: `fl_init` only fails to produce one when the process is out of
/// address space, and nothing further would work anyway.
pub fn borrow_base_arena(raw: *mut sys::Arena) -> Arena {
    assert!(
        !raw.is_null(),
        "flowi_core: fl_init failed to create the main arena"
    );
    // SAFETY: non-null, and owned by the foundation for as long as it is up - which is at
    // least as long as whatever this is stored in, since only shutdown takes it down and
    // that cannot run while a borrow is live.
    unsafe { Arena::from_raw_borrowed(raw.cast()) }
}

/// Bring the process-wide foundation up with `job_threads` background workers, and borrow
/// the main arena it created.
///
/// Every core entry point does this for itself, so a caller reaching core only through
/// [`Arena`] or [`Jobs`] never calls it. It is for the one that has to touch the foundation
/// first - a test harness, or an embedder placing its own allocations in the main arena.
/// Going through C `fl_init` directly instead would skip the lock that serialises bring-up.
///
/// Idempotent: a foundation that is already up is adopted and `job_threads` ignored. The
/// returned handle borrows the arena and never destroys it - that is [`shutdown`]'s job.
///
/// # Panics
/// Panics if the main arena could not be created, which means the process is out of address
/// space and nothing further would work anyway.
pub fn init(job_threads: i32) -> Arena {
    borrow_base_arena(init_shared(job_threads))
}

/// Tear the process-wide foundation down: the job system, the logging and perf tiers, the
/// main arena every long-lived allocation came from, and the calling thread's scratch
/// arenas.
///
/// Only for a host that wants a deterministic shutdown - a leak checker at process exit, or
/// a library embedding core that has to leave the process clean. Every arena handle must
/// already be dropped; nothing may touch core afterwards. A later constructor brings the
/// foundation back up, and calling this twice is a no-op.
pub fn shutdown() {
    let _guard = BASE_LOCK.lock().unwrap_or_else(|e| e.into_inner());

    let was_up = BASE_ARENA.swap(core::ptr::null_mut(), Ordering::AcqRel);
    if !was_up.is_null() {
        // SAFETY: the foundation was up, and the swap means nothing else will destroy it.
        unsafe { sys::fl_destroy() };
    }
}
