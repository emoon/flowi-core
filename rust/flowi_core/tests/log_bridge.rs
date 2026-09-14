//! Its own test binary, so no other logging interferes with the process-wide file sink and
//! the process-wide log global.

use flowi_core::{install_log_bridge, LogFile};

#[test]
fn bridge_routes_levels_and_targets_through_the_file_sink() {
    // A unique temp dir per process keeps the file sink isolated from any other run.
    let dir = std::env::temp_dir().join(format!("flowi_log_bridge_{}", std::process::id()));
    std::fs::create_dir_all(&dir).expect("create temp log dir");
    let dir_str = dir.to_str().expect("utf-8 temp path");

    install_log_bridge();
    let sink = LogFile::open(dir_str).expect("file logging failed to initialize");

    // The default channel level is Trace, so all five emit; log has no Fatal, so Error is
    // the most severe a Rust record reaches.
    log::trace!("trace v={}", 1);
    log::debug!("plain message");
    log::info!("hello {}", 42);
    log::warn!("warn {}%", 90);
    log::error!("code={} name={}", 7, "boom");

    // Named channels come from target:, one channel per target.
    log::info!(target: "AUDIO", "mixer running at {} Hz", 48000);
    log::error!(target: "PLUGIN", "plugin {} failed", "hello");

    sink.flush();

    let path = sink.path();
    assert!(!path.is_empty(), "no active log file path after open");
    let contents = std::fs::read_to_string(&path).expect("read log file");

    for expected in [
        "trace v=1",
        "plain message",
        "hello 42",
        "warn 90%",
        "code=7 name=boom",
        "mixer running at 48000 Hz",
        "plugin hello failed",
    ] {
        assert!(
            contents.contains(expected),
            "message {expected:?} not rendered: {contents}"
        );
    }

    // The C formatter brackets the channel name, so the untargeted records land on CORE
    // and the targeted ones on their own channels.
    for channel in ["[CORE", "[AUDIO", "[PLUGIN"] {
        assert!(
            contents.contains(channel),
            "channel {channel:?} missing from output: {contents}"
        );
    }

    // Levels map onto flowi's names - Warn onto WARNING, the one that is not a rename.
    for level in ["[TRACE", "[DEBUG", "[INFO", "[WARNING", "[ERROR"] {
        assert!(
            contents.contains(level),
            "level {level:?} missing from output: {contents}"
        );
    }

    // The bridge captures the call site, so records carry this file's name.
    assert!(
        contents.contains("log_bridge.rs:"),
        "source location missing from output: {contents}"
    );

    drop(sink);
    let _ = std::fs::remove_dir_all(&dir);
}
