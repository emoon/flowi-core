//! flowi-core's api_gen configuration, shared by the regenerate binary and the drift test.

use std::path::{Path, PathBuf};

use api_gen::naming::Naming;
use api_gen::Config;

/// One codegen target: what it is called, which directories it owns, and how to configure it.
pub struct Target {
    /// Human name, used in drift reports.
    pub name: &'static str,
    /// Repo-relative directories this target writes into.
    pub outputs: &'static [&'static str],
    /// The config, with outputs rooted at `out_root`.
    pub config: Config,
}

/// Every codegen target, with outputs rooted at `out_root`. The IDL always comes from
/// `repo`; the two roots are the same tree for a real regeneration, and differ only for the
/// drift test, which writes to a temp dir and compares.
pub fn targets(repo: &Path, out_root: &Path) -> Vec<Target> {
    vec![Target {
        name: "core API",
        outputs: &[
            "include/flowi/arena",
            "include/flowi/core",
            "include/flowi/heap",
            "include/flowi/path",
            "include/flowi/string",
            "rust/flowi_core_sys/src/generated",
            "exports",
        ],
        config: Config {
            naming: Naming::new("Fl", "fl"),
            api_dir: repo.join("api"),
            // The `flowi` prefix, not `flowi_core`: a header above core includes a core
            // type's header as <flowi/core/math_data.h>, and both repositories install
            // under that one prefix.
            c_include_root: Some(out_root.join("include/flowi")),
            c_include_prefix: Some("flowi".to_owned()),
            c_extern_c: true,
            rust_dir: Some(out_root.join("rust/flowi_core_sys/src/generated")),
            export_dir: Some(out_root.join("exports")),
            export_stem: Some("flowi_core".to_owned()),
            rust_allow_dead_code: true,
            gen_script: Some("cargo regen".to_owned()),
            ..Default::default()
        },
    }]
}

/// The repository root, one level above this crate.
pub fn repo_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .expect("codegen/ sits at the repo root")
        .to_path_buf()
}
