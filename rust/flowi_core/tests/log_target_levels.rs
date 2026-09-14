//! Its own test binary - both the file sink and the floor list are process-wide.

use flowi_core::{install_log_bridge, set_target_level, LogFile};

#[test]
fn target_floors_drop_records_below_their_level() {
    let dir = std::env::temp_dir().join(format!("flowi_log_targets_{}", std::process::id()));
    std::fs::create_dir_all(&dir).expect("create temp log dir");
    let dir_str = dir.to_str().expect("utf-8 temp path");

    install_log_bridge();
    let sink = LogFile::open(dir_str).expect("file logging failed to initialize");

    // A prefix covers everything under it.
    set_target_level("noisy_dep", log::LevelFilter::Warn);

    log::debug!(target: "noisy_dep::inner", "chatty detail nobody asked for");
    log::info!(target: "noisy_dep", "routine progress");
    log::warn!(target: "noisy_dep", "something actually went wrong");
    log::debug!(target: "QUIET", "kept because nothing floors this target");

    sink.flush();
    let contents = std::fs::read_to_string(sink.path()).expect("read log file");

    for dropped in ["chatty detail nobody asked for", "routine progress"] {
        assert!(
            !contents.contains(dropped),
            "floored record {dropped:?} reached the sink: {contents}"
        );
    }
    for kept in [
        "something actually went wrong",
        "kept because nothing floors this target",
    ] {
        assert!(
            contents.contains(kept),
            "record {kept:?} was dropped by an unrelated floor: {contents}"
        );
    }

    drop(sink);
    let _ = std::fs::remove_dir_all(&dir);
}
