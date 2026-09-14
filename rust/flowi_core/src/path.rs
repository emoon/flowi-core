//! Path manipulation over core's own rules, for the paths flowi itself deals in.
//!
//! std::path is the right tool for a path the OS handed you. These are for the
//! other kind: a path that came out of a manifest, an archive listing or a VFS
//! mount, where the answer has to be the one core's C side would give. Two rules
//! differ from std::path in ways that matter:
//!
//! * A backslash separates on every platform, Linux included - a path
//!   authored on Windows and shipped inside an archive still splits correctly.
//!   Separators are not rewritten, though: what comes back carries whichever
//!   ones went in, so parent(r"games\sub\boot.adf") is r"games\sub". Only the
//!   separator [`join`] inserts is always /.
//! * A URL is absolute. http:// and https:// count alongside a leading
//!   /, a drive letter and a UNC prefix, because a download link and a local
//!   path travel through the same field during content resolution.
//!
//! Each function copies its result out of a scratch arena, so nothing here borrows
//! from an arena the caller has to keep alive.
//!
//! ```
//! assert_eq!(flowi::path::join("/games", "boot.adf"), "/games/boot.adf");
//! assert_eq!(flowi::path::parent("/games/boot.adf"), "/games");
//! assert!(flowi::path::is_absolute("https://example.com/x.zip"));
//! ```

use flowi_core_sys as sys;

use crate::Arena;

/// Join relative onto base. An absolute relative wins outright, so this is
/// the "resolve a name that may already be a full path" join - not the
/// concatenation str would give you.
#[inline]
pub fn join(base: &str, relative: &str) -> String {
    Arena::scratch(|arena| {
        // SAFETY: both borrows outlive the call, and the result is copied out of
        // the scratch arena before it is released.
        unsafe {
            sys::path_join(
                arena.as_raw(),
                sys::RawStr::borrow(base),
                sys::RawStr::borrow(relative),
            )
        }
        .into_string()
    })
}

/// The parent directory of path, or an empty string when it has none - a bare
/// filename, or the root itself.
#[inline]
pub fn parent(path: &str) -> String {
    Arena::scratch(|arena| {
        // SAFETY: the borrow outlives the call, and the result is copied out of
        // the scratch arena before it is released.
        unsafe { sys::path_get_parent(arena.as_raw(), sys::RawStr::borrow(path)) }.into_string()
    })
}

/// Whether path is absolute: a leading /, a Windows drive letter or UNC
/// prefix, or an http:// / https:// URL.
#[inline]
pub fn is_absolute(path: &str) -> bool {
    // SAFETY: the borrow outlives the call, which only reads the bytes.
    unsafe { sys::path_is_absolute(sys::RawStr::borrow(path)) }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn joining_inserts_exactly_one_separator() {
        assert_eq!(join("/games", "boot.adf"), "/games/boot.adf");
        assert_eq!(join("/games/", "boot.adf"), "/games/boot.adf");
        assert_eq!(join("/games", "sub/boot.adf"), "/games/sub/boot.adf");
    }

    /// An empty component contributes nothing - the caller gets the other half
    /// back rather than a path with a dangling separator.
    #[test]
    fn joining_an_empty_component_yields_the_other() {
        assert_eq!(join("", "boot.adf"), "boot.adf");
        assert_eq!(join("/games", ""), "/games");
    }

    /// The reason this exists rather than string concatenation: a relative that
    /// is already a full path replaces the base instead of being appended to it.
    #[test]
    fn an_absolute_relative_component_wins_outright() {
        assert_eq!(join("/games", "/cache/boot.adf"), "/cache/boot.adf");
        assert_eq!(join("/games", "https://host/x.zip"), "https://host/x.zip");
    }

    #[test]
    fn the_parent_is_everything_before_the_last_separator() {
        assert_eq!(parent("/games/boot.adf"), "/games");
        assert_eq!(parent("/games/sub/boot.adf"), "/games/sub");
        // A trailing separator is not a component of its own.
        assert_eq!(parent("/games/sub/"), "/games");
    }

    /// A path with no parent answers empty rather than inventing . or / -
    /// callers branch on the empty string.
    #[test]
    fn a_path_without_a_parent_is_empty() {
        assert_eq!(parent("boot.adf"), "");
        assert_eq!(parent(""), "");
    }

    /// A backslash separates on every platform - the Windows-authored archive
    /// path case - but the separators that come back are the ones that went in.
    /// Nothing is rewritten, and a caller comparing the result against a
    /// forward-slash literal would be wrong to expect otherwise.
    #[test]
    fn a_backslash_separates_but_is_not_rewritten() {
        assert_eq!(parent(r"games\sub\boot.adf"), r"games\sub");
        // Only the separator join inserts is a forward slash; the base is
        // reproduced as-is...
        assert_eq!(join(r"games\sub", "boot.adf"), r"games\sub/boot.adf");
        // ...and a trailing backslash already counts as one, so none is added.
        assert_eq!(join(r"games/sub\", "boot.adf"), r"games/sub\boot.adf");
    }

    /// The root survives as a parent rather than collapsing to the empty string
    /// a bare filename gets: a file directly under / has a parent, and it is
    /// /. Callers branch on empty to mean "no parent at all", so the two cases
    /// have to stay distinguishable.
    #[test]
    fn a_file_at_the_root_has_the_root_as_its_parent() {
        assert_eq!(parent("/boot.adf"), "/");
        assert_eq!(parent("/"), "/");
    }

    /// Windows-shaped absolute paths, which reach this code from archive
    /// listings and manifests even on Linux.
    #[test]
    fn drive_letters_and_unc_prefixes_are_absolute() {
        assert!(is_absolute(r"C:\games\boot.adf"));
        assert!(is_absolute(r"\\host\share\boot.adf"));
    }

    /// Core counts a URL as absolute, which std::path does not.
    #[test]
    fn absoluteness_covers_roots_drives_and_urls() {
        assert!(is_absolute("/games/boot.adf"));
        assert!(is_absolute("C:/games/boot.adf"));
        assert!(is_absolute("http://example.com/x.zip"));
        assert!(is_absolute("https://example.com/x.zip"));

        assert!(!is_absolute("games/boot.adf"));
        assert!(!is_absolute("boot.adf"));
        assert!(!is_absolute(""));
    }
}
