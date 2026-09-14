//! Scheduling calls do not bootstrap the foundation, so every test here brings it up first.

use flowi_core::{JobHandle, JobPriority, Jobs, JobsResult, WorkerInfo};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

/// Idempotent, so each test in this binary calls it.
fn foundation() {
    let _arena = flowi_core::init(1);
}

#[test]
fn job_runs_closure_exactly_once_and_waits() {
    foundation();
    let counter = Arc::new(AtomicUsize::new(0));
    let worker = Arc::new(AtomicUsize::new(usize::MAX));

    let c = counter.clone();
    let w = worker.clone();
    let handle: JobHandle = Jobs::add(move |info: WorkerInfo| {
        c.fetch_add(1, Ordering::SeqCst);
        w.store(info.worker_index as usize, Ordering::SeqCst);
    });

    Jobs::wait(handle);

    // The trampoline reclaims the box after the single call.
    assert_eq!(
        counter.load(Ordering::SeqCst),
        1,
        "closure should run exactly once"
    );

    let idx = worker.load(Ordering::SeqCst);
    let threads = Jobs::num_threads() as usize;
    assert!(threads >= 1, "job system should have at least one worker");
    assert!(
        idx < threads,
        "worker index {idx} out of range 0..{threads}"
    );
}

#[test]
fn dependency_orders_two_jobs() {
    foundation();
    let order = Arc::new(AtomicUsize::new(0));

    let o1 = order.clone();
    let first = Jobs::add(move |_| {
        let _ = o1.compare_exchange(0, 1, Ordering::SeqCst, Ordering::SeqCst);
    });

    let o2 = order.clone();
    let second = Jobs::add_after(
        move |_| {
            // Only advances to 2 if first already set 1.
            let _ = o2.compare_exchange(1, 2, Ordering::SeqCst, Ordering::SeqCst);
        },
        first,
    );

    Jobs::wait(second);
    assert_eq!(
        order.load(Ordering::SeqCst),
        2,
        "dependent job must run after its dependency"
    );
}

#[test]
fn query_surface_is_safe() {
    foundation();
    assert!(Jobs::is_main_thread(), "test thread is the main thread");

    let done = Arc::new(AtomicUsize::new(0));
    let d = done.clone();
    let handle = Jobs::add(move |_| {
        d.fetch_add(1, Ordering::SeqCst);
    });
    Jobs::wait(handle);

    // After completion the handle reports Finished, or Invalid once recycled.
    assert!(
        matches!(
            Jobs::is_finished(handle),
            JobsResult::Finished | JobsResult::Invalid
        ),
        "a waited-on job is not still pending"
    );

    assert!(
        !Jobs::set_priority(JobHandle::INVALID, JobPriority::Low),
        "an invalid handle cannot be reprioritized"
    );
    assert_eq!(
        Jobs::get_priority(JobHandle::INVALID),
        -1,
        "an invalid handle has priority -1"
    );
}

#[test]
fn add_with_priority_runs_the_closure() {
    foundation();
    let ran = Arc::new(AtomicUsize::new(0));
    let r = ran.clone();
    let handle = Jobs::add_with_priority(
        move |_| {
            r.fetch_add(1, Ordering::SeqCst);
        },
        JobPriority::High,
    );
    Jobs::wait(handle);
    assert_eq!(ran.load(Ordering::SeqCst), 1, "high-priority job ran once");
}

#[test]
fn panicking_closure_is_contained() {
    foundation();
    // A panic inside a job must not unwind across the C boundary (that would abort the
    // process) - the trampoline's catch_unwind swallows it. The panic prints a message
    // to stderr; that noise is expected, not a test failure.
    let panicked = Arc::new(AtomicUsize::new(0));
    let p = panicked.clone();
    let bad = Jobs::add(move |_| {
        p.fetch_add(1, Ordering::SeqCst);
        panic!("job blew up on purpose");
    });
    Jobs::wait(bad);
    assert_eq!(
        panicked.load(Ordering::SeqCst),
        1,
        "the panicking closure did execute"
    );

    let ok = Arc::new(AtomicUsize::new(0));
    let o = ok.clone();
    let good = Jobs::add(move |_| {
        o.fetch_add(1, Ordering::SeqCst);
    });
    Jobs::wait(good);
    assert_eq!(
        ok.load(Ordering::SeqCst),
        1,
        "the worker survived the panic and ran the next job"
    );
}
