//! The log-crate bridge onto flowi core's channel logging, plus the RAII file sink.
//!
//! Rust code logs with the log crate - log::info!("..."), log::warn!(target: "AUDIO", "...") -
//! and this module is the sink those records land in. Each record's target: names a
//! flowi log channel 1:1; a record without an explicit target (its target is the module
//! path the macro captured) goes to "CORE", the channel C's own `fl_log_*` convenience
//! macros use. C and Rust output therefore interleave in one sink with identical
//! formatting, and fl_log_channel_enable / fl_log_channel_set_level filter both sides
//! by the same channel id.
//!
//! Channel ids are cached here and registered on first use. fl_log_register_channel
//! dedups by name, so whichever side registers first wins and the other resolves to the
//! same id.
//!
//! flowi core's channel-less entry points (fl_log_message and siblings) are C variadics,
//! which carry no Rust binding. Formatting therefore happens Rust-side and the finished
//! text goes to fl_log_c_message_formatted, the one pre-formatted C emitter.
//!
//! [`install_log_bridge`] is what wires the two together. The host gets it for free from
//! [`init`](crate::init) and every other flowi entry point; a dlopen'd Rust cdylib has
//! its own log global and must install it for itself.
//!
//! Everything reaches the sink by default, third-party crates included. [`set_target_level`]
//! is the floor for that: it is to a log target what fl_log_channel_set_level is to a
//! channel, and it drops a record before it is ever formatted.

use flowi_core_sys as sys;
use std::collections::BTreeMap;
use std::ffi::CString;
use std::ptr;
use std::sync::{Mutex, Once};

/// The channel a record without an explicit target: lands on - the same default channel
/// C's `fl_log_*` macros use.
const DEFAULT_CHANNEL: &str = "CORE";

/// Channel name -> id, so a repeat target costs a map lookup instead of an FFI call. Keys are
/// leaked (see [`channel_id`]), which bounds the leak by the number of distinct targets - and
/// flowi core caps the channel table at 32 entries anyway.
static CHANNELS: Mutex<BTreeMap<&'static str, u32>> = Mutex::new(BTreeMap::new());

/// Target-prefix floors, keyed by prefix so setting the same one twice replaces it and the
/// lookup can pick the most specific match whatever order they were installed in.
static TARGET_LEVELS: Mutex<BTreeMap<&'static str, log::LevelFilter>> = Mutex::new(BTreeMap::new());

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// The id name is registered under, registering it on first use.
///
/// The name is leaked into 'static because flowi core stores the pointer in its channel
/// table for the rest of the process; a borrowed name would dangle the moment the record
/// that carried it went away.
fn channel_id(name: &str) -> u32 {
    let mut channels = CHANNELS.lock().unwrap_or_else(|e| e.into_inner());
    if let Some(id) = channels.get(name) {
        return *id;
    }

    let name: &'static str = Box::leak(name.to_owned().into_boxed_str());
    // SAFETY: name is 'static, satisfying C's contract that a channel name outlives the
    // table entry it is stored in.
    let id = unsafe { sys::fl_log_register_channel(sys::RawStr::from_static(name)) };
    channels.insert(name, id);
    id
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// Map a log level onto flowi's. LogLevel::Fatal has no log counterpart, so it is
/// reachable only from C - log::error! is the most severe a Rust record can be.
fn to_fl_level(level: log::Level) -> sys::LogLevel {
    match level {
        log::Level::Error => sys::LogLevel::Error,
        log::Level::Warn => sys::LogLevel::Warning,
        log::Level::Info => sys::LogLevel::Info,
        log::Level::Debug => sys::LogLevel::Debug,
        log::Level::Trace => sys::LogLevel::Trace,
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// The log::Log impl: render Rust-side, then emit on the record's channel.
struct ChannelLogger;

impl log::Log for ChannelLogger {
    /// Only the target floors [`set_target_level`] installed are answered here. The channel
    /// level and enable flag belong to flowi core, which applies them inside the emitter.
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        let floors = TARGET_LEVELS.lock().unwrap_or_else(|e| e.into_inner());
        // The most specific floor wins: ureq::tls overrides a broader ureq for a target
        // both match, whichever order they were installed in.
        match floors
            .iter()
            .filter(|(prefix, _)| metadata.target().starts_with(*prefix))
            .max_by_key(|(prefix, _)| prefix.len())
        {
            Some((_, level)) => metadata.level() <= *level,
            None => true,
        }
    }

    fn log(&self, record: &log::Record<'_>) {
        // log's contract is that a logger filters for itself: the macros check enabled
        // first, but a caller reaching log::logger().log(...) directly does not.
        if !self.enabled(record.metadata()) {
            return;
        }

        // A record with no explicit target: carries the module path in both slots; that is
        // the one case that means "the caller did not name a channel".
        let target = record.target();
        let channel = if Some(target) == record.module_path() {
            DEFAULT_CHANNEL
        } else {
            target
        };

        let message = std::fmt::format(*record.args());
        // The C emitter wants a NUL-terminated path; log hands out a plain &str. A file
        // name with an interior NUL is not a path any compiler emits, so dropping the
        // location in that case costs nothing real.
        let file = record.file().and_then(|path| CString::new(path).ok());

        // SAFETY: the channel id is one flowi core handed back (or a benign invalid id it
        // rejects), file is either null or NUL-terminated, and both it and the borrowed
        // message outlive this synchronous call.
        unsafe {
            sys::fl_log_c_message_formatted(
                channel_id(channel),
                to_fl_level(record.level()),
                file.as_ref().map_or(ptr::null(), |path| path.as_ptr()),
                record.line().unwrap_or(0) as i32,
                sys::RawStr::borrow(&message),
            );
        }
    }

    fn flush(&self) {
        // SAFETY: a plain global call with no arguments to validate.
        unsafe { sys::fl_log_flush() }
    }
}

static LOGGER: ChannelLogger = ChannelLogger;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// Route this binary's log records at flowi core's channel logging.
///
/// Call this before the first record you care about. Until it runs, log sits at
/// [`LevelFilter::Off`](log::LevelFilter::Off) and discards every record without formatting
/// it - a log call before the install is not merely unsinked, it is gone. Every flowi entry
/// point installs the bridge as the foundation comes up, but a host that logs before it
/// first reaches flowi - parsing a command line, reporting why startup is about to fail -
/// must install it itself, as the first thing main does.
///
/// A dlopen'd Rust cdylib always has to: a shared object statically links its own copy of
/// the log crate, so its log global is one the host's install never touched.
///
/// Idempotent, and it never fights a logger that is already installed - a host that set up
/// its own log sink before reaching flowi keeps it, and flowi's records flow there
/// instead.
pub fn install_log_bridge() {
    static INSTALL: Once = Once::new();
    INSTALL.call_once(|| {
        if log::set_logger(&LOGGER).is_ok() {
            // Filtering is flowi core's job, per channel, so pass everything down to it.
            log::set_max_level(log::LevelFilter::Trace);
        }
    });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// Drop records whose target starts with prefix below level.
///
/// A log target defaults to the emitting module's path, so "ureq" covers a whole
/// dependency and "ureq::tls" one part of it.
///
/// When two floors both match a target, the longer - more specific - prefix wins, so
/// ("ureq", Warn) plus ("ureq::tls", Debug) keeps the TLS layer chatty inside an
/// otherwise quiet dependency regardless of which was set first. Setting the same prefix
/// again replaces its level. The check runs before a record is formatted, so a floored
/// target costs nothing beyond the lookup.
pub fn set_target_level(prefix: &'static str, level: log::LevelFilter) {
    let mut floors = TARGET_LEVELS.lock().unwrap_or_else(|e| e.into_inner());
    floors.insert(prefix, level);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// The on-disk log sink, open for as long as this guard lives.
///
/// [`open`](LogFile::open) starts writing every emitted record - C's and Rust's alike - to a
/// file under base_dir; dropping the guard flushes and closes it. The sink is process-wide,
/// so exactly one guard owns it: hold it in main for the lifetime of the program and let
/// the drop at the end of scope be the shutdown.
#[derive(Debug)]
pub struct LogFile {
    /// Not constructible from outside - open is the only way to get one, so holding a
    /// LogFile proves the sink is open.
    _private: (),
}

impl LogFile {
    /// Open the on-disk sink under base_dir, or None when the file could not be created.
    ///
    /// An empty base_dir selects flowi's default data directory. On failure logging stays
    /// console-only.
    pub fn open(base_dir: &str) -> Option<LogFile> {
        // SAFETY: the borrowed base_dir outlives this synchronous call; C copies the path
        // it keeps into its own arena.
        let opened = unsafe { sys::fl_log_file_init(sys::RawStr::borrow(base_dir)) };
        opened.then_some(LogFile { _private: () })
    }

    /// The path of the file being written.
    pub fn path(&self) -> String {
        // SAFETY: the returned string points at C's log arena, which outlives this call; the
        // conversion copies out of it.
        unsafe { sys::fl_log_file_get_path() }.into_string()
    }

    /// Flush buffered output to disk without closing the sink.
    #[inline]
    pub fn flush(&self) {
        // SAFETY: a plain global call with no arguments to validate.
        unsafe { sys::fl_log_flush() }
    }
}

impl Drop for LogFile {
    fn drop(&mut self) {
        // SAFETY: a plain global call; C tolerates a shutdown with no file open.
        unsafe { sys::fl_log_file_shutdown() }
    }
}
