//! Runtime coverage for the typed arena API over the real FlArenaApi FFI.

use flowi_core::{afmt, Arena, ArenaString, ArenaVec};

const RESERVE: u64 = 1 << 20;

#[test]
fn persistent_entries_across_frames() {
    let arena = Arena::new(RESERVE);

    let mut entries = ArenaVec::new(&arena);
    for frame in 0..5u32 {
        entries.push(frame * 10);
    }
    let entries = entries.into_slice();
    assert_eq!(entries, &[0, 10, 20, 30, 40]);

    let a = arena.alloc(42u32);
    let b = arena.alloc(7u32);
    assert_eq!(*a + *b, 49);
    assert_eq!(entries[2], 20);
}

/// The &str borrow must end before rewind takes &mut self - hence the inner scope.
#[test]
fn per_frame_strings_with_rewind() {
    let mut arena = Arena::new(RESERVE);

    for frame in 0..4u32 {
        {
            let label = afmt!(&arena, "frame {frame}");
            assert_eq!(label, format!("frame {frame}"));
        }
        arena.rewind();
    }
}

#[test]
fn temp_scope_reclaims() {
    let mut arena = Arena::new(RESERVE);

    let total = arena.temp(|t| {
        let mut v = ArenaVec::new(t);
        for i in 0..10i32 {
            v.push(i);
        }
        let s = afmt!(t, "n={}", v.len());
        assert_eq!(s, "n=10");
        v.as_slice().iter().sum::<i32>()
    });
    assert_eq!(total, 45);

    let x = arena.alloc(99u32);
    assert_eq!(*x, 99);
}

/// scratch_avoiding guarantees the scratch arena is a different underlying arena
/// than the one passed in.
#[test]
fn scratch_pool() {
    let out = Arena::scratch(|s| {
        let msg = afmt!(s, "scratch {}", 7);
        assert_eq!(msg, "scratch 7");
        msg.len()
    });
    assert_eq!(out, "scratch 7".len());

    let arena = Arena::new(RESERVE);
    let squares: &[i32] = Arena::scratch_avoiding(&arena, |s| {
        let mut v = ArenaVec::new(s);
        for i in 1..=4i32 {
            v.push(i * i);
        }
        arena.alloc_slice_copy(v.into_slice())
    });
    assert_eq!(squares, &[1, 4, 9, 16]);
}

/// ArenaVec growth is allocate-copy-abandon: pushing past capacity relocates the
/// elements into a fresh block.
#[test]
fn arena_vec_growth_preserves_elements() {
    let arena = Arena::new(RESERVE);
    let mut v = ArenaVec::with_capacity(&arena, 2);
    assert!(v.is_empty());
    for i in 0..64u64 {
        v.push(i);
    }
    assert_eq!(v.len(), 64);
    let s = v.into_slice();
    assert_eq!(s[0], 0);
    assert_eq!(s[63], 63);
    assert_eq!(s.iter().sum::<u64>(), (0..64).sum());
}

#[test]
fn alloc_zeroed_is_zero() {
    let arena = Arena::new(RESERVE);
    let z: &mut [u32; 8] = arena.alloc_zeroed();
    assert_eq!(z, &[0u32; 8]);
    z[3] = 5;
    assert_eq!(z[3], 5);
}

#[test]
fn arena_string_write_and_freeze() {
    use core::fmt::Write;
    let arena = Arena::new(RESERVE);
    let mut s = ArenaString::new(&arena);
    write!(s, "{}+{}={}", 2, 3, 5).unwrap();
    assert_eq!(s.as_str(), "2+3=5");
    let frozen: &str = s.into_str();
    assert_eq!(frozen, "2+3=5");
}

/// Values moved into the arena are never dropped - they are abandoned (leaked), not
/// destructed, at rewind, temp-scope end, scratch-scope end, and arena destroy.
#[test]
fn drop_never_runs_on_arena_contents() {
    use std::cell::Cell;

    struct DropFlag<'a>(&'a Cell<bool>);
    impl Drop for DropFlag<'_> {
        fn drop(&mut self) {
            self.0.set(true);
        }
    }

    let fired = Cell::new(false);

    // let _ = ends the loan so rewind can take &mut.
    {
        let mut arena = Arena::new(RESERVE);
        let _ = arena.alloc(DropFlag(&fired));
        arena.rewind();
        assert!(!fired.get(), "rewind must not drop arena contents");
    }

    {
        let mut arena = Arena::new(RESERVE);
        arena.temp(|t| {
            let _ = t.alloc(DropFlag(&fired));
        });
        assert!(!fired.get(), "temp scope end must not drop arena contents");
    }

    Arena::scratch(|s| {
        let _ = s.alloc(DropFlag(&fired));
    });
    assert!(
        !fired.get(),
        "scratch scope end must not drop arena contents"
    );

    {
        let arena = Arena::new(RESERVE);
        let _ = arena.alloc(DropFlag(&fired));
    }
    assert!(!fired.get(), "arena destroy must not drop arena contents");
}

#[test]
fn alloc_str_copies_into_arena() {
    let arena = Arena::new(RESERVE);
    let s = arena.alloc_str("hello");
    assert_eq!(s, "hello");
    s.make_ascii_uppercase();
    assert_eq!(s, "HELLO");
}
