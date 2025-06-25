# user_thread_scheduler
Preemptive user-level thread library in C with round-robin scheduling, context switching via sigsetjmp/siglongjmp, and virtual timing.

This project implements a preemptive **user-level thread library in C**, supporting multiple threads and round-robin scheduling, with context switching and signal-based time slicing. No OS kernel thread API is used — the library provides its own thread management with fully-controlled scheduling logic.

---

## Core Features

- Round-Robin scheduler using virtual timer (setitimer / SIGVTALRM)
- sigsetjmp / siglongjmp used for low-level context switching  
- Supports blocking, resuming, termination, and dynamic thread ID reuse  
- Thread states: RUNNING, READY, BLOCKED — managed with internal queues  
- Precise control over thread switching and signal masking

---

## Files

- `uthread.c`: Core thread library logic (scheduling, switching, timers)
- `uthread.h`: Public API (not to be edited)
- `main.c`: Test/demo driver for thread execution
- `Makefile`: Compiles the library into `libuthreads.a`
- `README.md`: Project overview and theoretical explanations

---

## Usage

To compile the project:

```bash
make
```

This produces a static library `libuthreads.a`, which can be linked against your test program.

---

## Notes

- No OS thread calls (e.g., pthreads) used — all threads are scheduled at the user level  
- Stack switching, timer-based preemption, and blocking/resuming are implemented manually  
- Virtual time used for preemption (real-time is ignored)  
- Handles edge cases: nested blocking, terminated main thread, and ID reuse  
```
