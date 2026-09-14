//! When two set_target_level floors both match a record's target, the more specific one
//! wins - whichever order they were installed in - and re-setting a prefix replaces its
//! level rather than leaving the old one behind. Its own test binary: both the file sink
//! and the floor list are process-wide.

use flowi_core::{install_log_bridge, set_target_level, LogFile};

#[test]
fn longest_matching_prefix_wins_regardless_of_order() {
    let dir = std::env::temp_dir().join(format!("flowi_log_specific_{}", std::process::id()));
    std::fs::create_dir_all(&dir).expect("create temp log dir");
    let dir_str = dir.to_str().expect("utf-8 temp path");

    install_log_bridge();
    let sink = LogFile::open(dir_str).expect("file logging failed to initialize");

    // Specific first, then broader.
    set_target_level("quiet_dep::loud", log::LevelFilter::Debug);
    set_target_level("quiet_dep", log::LevelFilter::Warn);
    log::debug!(target: "quiet_dep::loud", "specific floor installed first");
    log::debug!(target: "quiet_dep::other", "only the broad floor covers this");

    // Broader first, then specific.
    set_target_level("hushed_dep", log::LevelFilter::Warn);
    set_target_level("hushed_dep::loud", log::LevelFilter::Debug);
    log::debug!(target: "hushed_dep::loud", "specific floor installed second");
    log::debug!(target: "hushed_dep::other", "again just the broad floor");

    // Re-setting a prefix replaces its level instead of stacking behind the old entry.
    set_target_level("hushed_dep::loud", log::LevelFilter::Warn);
    log::debug!(target: "hushed_dep::loud", "silenced by the replacement floor");

    sink.flush();
    let contents = std::fs::read_to_string(sink.path()).expect("read log file");

    for kept in [
        "specific floor installed first",
        "specific floor installed second",
    ] {
        assert!(
            contents.contains(kept),
            "a broader floor overrode the more specific one: {contents}"
        );
    }
    for dropped in [
        "only the broad floor covers this",
        "again just the broad floor",
        "silenced by the replacement floor",
    ] {
        assert!(
            !contents.contains(dropped),
            "floored record {dropped:?} reached the sink: {contents}"
        );
    }

    drop(sink);
    let _ = std::fs::remove_dir_all(&dir);
}
