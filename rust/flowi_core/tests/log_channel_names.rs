//! flowi core keeps a channel-name pointer in its table for the rest of the process, so
//! the bridge owns the names it registers rather than borrowing the record's.
//!
//! Its own test binary - the file sink it reads back is process-wide.

use flowi_core::{install_log_bridge, LogFile};

#[test]
fn channel_names_outlive_the_record_that_named_them() {
    let dir = std::env::temp_dir().join(format!("flowi_log_channels_{}", std::process::id()));
    std::fs::create_dir_all(&dir).expect("create temp log dir");
    let dir_str = dir.to_str().expect("utf-8 temp path");

    install_log_bridge();
    let sink = LogFile::open(dir_str).expect("file logging failed to initialize");

    {
        let transient = String::from("TRANSIENT");
        log::info!(target: transient.as_str(), "registered from a temporary");
    }
    // A second record on the same channel resolves through the cache.
    log::info!(target: "TRANSIENT", "still the same channel");

    sink.flush();
    let contents = std::fs::read_to_string(sink.path()).expect("read log file");

    assert_eq!(
        contents.matches("[TRANSIENT").count(),
        2,
        "channel name did not survive its record: {contents}"
    );

    drop(sink);
    let _ = std::fs::remove_dir_all(&dir);
}
